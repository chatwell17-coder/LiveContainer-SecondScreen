// Experimental in-process touch delivery, restricted to the selected guest window.
// SPDX-License-Identifier: AGPL-3.0-only
// Private selector/protocol reference: KIF's UITouch-KIFAdditions.m and IOHIDEvent+KIF.m.
#import <objc/message.h>
#import <dlfcn.h>
#import <mach/mach_time.h>

@interface UITouch (LCRemotePrivate)
- (void)setWindow:(UIWindow *)window;
- (void)setView:(id)view;
- (void)setGestureView:(id)view;
- (void)setTapCount:(NSUInteger)count;
- (void)setTimestamp:(NSTimeInterval)timestamp;
- (void)setPhase:(UITouchPhase)phase;
- (void)_setLocationInWindow:(CGPoint)point resetPrevious:(BOOL)reset;
- (void)_setIsFirstTouchForView:(BOOL)first;
- (void)_setHidEvent:(CFTypeRef)event;
@end
@interface UIEvent (LCRemotePrivate)
- (void)_clearTouches;
- (void)_addTouch:(UITouch *)touch forDelayedDelivery:(BOOL)delayed;
- (void)_setHIDEvent:(CFTypeRef)event;
@end
@interface UIApplication (LCRemotePrivate)
- (UIEvent *)_touchesEvent;
@end
@interface UIView (LCRemotePrivate)
- (id)_hitTestWithContext:(id)context;
@end

// No global HID dispatch or accessibility settings are changed. UIKit receives
// only a touch whose view/window belong to this guest process.
static CFTypeRef LCRemoteHID(UITouch *touch) CF_RETURNS_RETAINED;
static CFTypeRef LCRemoteHID(UITouch *touch) {
    typedef CFTypeRef (*HandFn)(CFAllocatorRef, uint64_t, uint32_t, uint32_t, uint32_t,
        uint32_t, uint32_t, double, double, double, double, double, Boolean, Boolean, uint32_t);
    typedef CFTypeRef (*FingerFn)(CFAllocatorRef, uint64_t, uint32_t, uint32_t, uint32_t,
        double, double, double, double, double, double, double, double, double, double,
        Boolean, Boolean, uint32_t);
    typedef void (*IntegerFn)(CFTypeRef, uint32_t, CFIndex);
    typedef void (*AppendFn)(CFTypeRef, CFTypeRef, uint32_t);
    static HandFn handFn; static FingerFn fingerFn; static IntegerFn integerFn; static AppendFn appendFn;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        void *handle = dlopen("/System/Library/Frameworks/IOKit.framework/IOKit", RTLD_LAZY | RTLD_LOCAL);
        if (!handle) handle = dlopen("/System/Library/PrivateFrameworks/IOKit.framework/IOKit", RTLD_LAZY | RTLD_LOCAL);
        if (!handle) return;
        handFn = (HandFn)dlsym(handle, "IOHIDEventCreateDigitizerEvent");
        fingerFn = (FingerFn)dlsym(handle, "IOHIDEventCreateDigitizerFingerEventWithQuality");
        integerFn = (IntegerFn)dlsym(handle, "IOHIDEventSetIntegerValue");
        appendFn = (AppendFn)dlsym(handle, "IOHIDEventAppendEvent");
    });
    if (!handFn || !fingerFn || !integerFn || !appendFn) return NULL;
    BOOL down = touch.phase != UITouchPhaseEnded && touch.phase != UITouchPhaseCancelled;
    uint32_t mask = touch.phase == UITouchPhaseMoved ? 4 : 3;
    CGPoint p = [touch locationInView:touch.window];
    uint64_t timestamp = mach_absolute_time();
    CFTypeRef hand = handFn(kCFAllocatorDefault, timestamp, 3, 0, 0, mask, 0, 0, 0, 0, 0, 0, down, down, 0);
    if (!hand) return NULL;
    CFTypeRef finger = fingerFn(kCFAllocatorDefault, timestamp, 1, 2, mask,
        p.x, p.y, 0, 0, 0, 5, 5, 1, 1, 1, down, down, 0);
    if (!finger) { CFRelease(hand); return NULL; }
    // Digitizer field base (11 << 16), display-integrated field offset 25.
    integerFn(hand, (11 << 16) + 25, 1);
    integerFn(finger, (11 << 16) + 25, 1);
    appendFn(hand, finger, 0);
    CFRelease(finger);
    return hand;
}

static UITouch *LCRemoteBegin(UIWindow *window, CGPoint point) {
    UITouch *touch = [UITouch new];
    for (NSString *name in @[@"setWindow:", @"setView:", @"setTapCount:", @"setTimestamp:", @"setPhase:",
                             @"_setLocationInWindow:resetPrevious:", @"_setHidEvent:"]) {
        if (![touch respondsToSelector:NSSelectorFromString(name)]) return nil;
    }
    id target = [window hitTest:point withEvent:nil];
    Class contextClass = NSClassFromString(@"_UIHitTestContext");
    SEL makeContext = NSSelectorFromString(@"contextWithPoint:radius:");
    if (contextClass && [contextClass respondsToSelector:makeContext]) {
        id context = ((id (*)(id, SEL, CGPoint, CGFloat))objc_msgSend)(contextClass, makeContext, point, 0);
        for (UIView *view = target; context && view; view = view.superview) {
            if (![view respondsToSelector:@selector(_hitTestWithContext:)]) continue;
            id found = [view _hitTestWithContext:context];
            if (found) { target = found; break; }
        }
    }
    if (!target) return nil;
    [touch setWindow:window];
    [touch setView:target];
    [touch setTapCount:1];
    [touch _setLocationInWindow:point resetPrevious:YES];
    [touch setPhase:UITouchPhaseBegan];
    if ([touch respondsToSelector:@selector(_setIsFirstTouchForView:)]) [touch _setIsFirstTouchForView:YES];
    if ([touch respondsToSelector:@selector(setGestureView:)]) [touch setGestureView:target];
    return touch;
}

static BOOL LCRemoteDeliver(UITouch *touch, CGPoint point, UITouchPhase phase) {
    if (!touch || !touch.window) return NO;
    UIApplication *app = UIApplication.sharedApplication;
    if (![app respondsToSelector:@selector(_touchesEvent)]) return NO;
    UIEvent *event = [app _touchesEvent];
    for (NSString *name in @[@"_clearTouches", @"_addTouch:forDelayedDelivery:", @"_setHIDEvent:"]) {
        if (![event respondsToSelector:NSSelectorFromString(name)]) return NO;
    }
    [touch _setLocationInWindow:point resetPrevious:phase == UITouchPhaseBegan];
    [touch setPhase:phase];
    [touch setTimestamp:NSProcessInfo.processInfo.systemUptime];
    CFTypeRef hid = LCRemoteHID(touch);
    if (!hid) return NO;
    @try {
        [touch _setHidEvent:hid];
        [event _clearTouches];
        [event _setHIDEvent:hid];
        [event _addTouch:touch forDelayedDelivery:NO];
        [app sendEvent:event];
    } @finally { CFRelease(hid); }
    return YES;
}
