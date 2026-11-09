// Experimental, opt-in guest-window capture. Included only by TweakLoader.m.
// SPDX-License-Identifier: AGPL-3.0-only
#import <UIKit/UIKit.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#import "LCRemoteTouch.h"

@interface LCRemoteTestView : UIView
@property(nonatomic, strong) UILabel *status;
@property(nonatomic) NSUInteger taps;
@end
@implementation LCRemoteTestView
- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    self.taps++;
    self.status.text = [NSString stringWithFormat:@"Touch began: %lu", (unsigned long)self.taps];
}
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    CGPoint p = [touches.anyObject locationInView:self];
    self.status.text = [NSString stringWithFormat:@"Dragging: %.0f, %.0f", p.x, p.y];
}
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { self.status.text = @"Touch ended"; }
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { self.status.text = @"Touch cancelled"; }
@end

@interface LCGuestDisplayProbe : NSObject
@property(nonatomic, weak) UIWindow *window;
@property(nonatomic, strong) NSTimer *discoveryTimer;
@property(nonatomic, strong) NSTimer *captureTimer;
@property(atomic, copy) NSData *frame;
@property(nonatomic) int listener;
@property(nonatomic) NSUInteger sequence;
@property(nonatomic, strong) UITouch *remoteTouch;
@property(nonatomic, strong) NSTimer *touchTimeout;
@property(nonatomic, strong) UIView *testPanel;
@end

static BOOL LCProbeSend(int fd, NSData *data) {
    const char *p = data.bytes;
    NSUInteger remaining = data.length;
    while (remaining) {
        ssize_t count = send(fd, p, remaining, 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return NO;
        p += count;
        remaining -= count;
    }
    return YES;
}

@implementation LCGuestDisplayProbe
- (void)cancelTouch {
    [self.touchTimeout invalidate]; self.touchTimeout = nil;
    UITouch *touch = self.remoteTouch; self.remoteTouch = nil;
    if (touch) {
        @try { LCRemoteDeliver(touch, [touch locationInView:touch.window], UITouchPhaseCancelled); }
        @catch (NSException *exception) { NSLog(@"Guest touch cancellation failed: %@", exception.name); }
    }
}
- (void)showInputTest {
    if (self.testPanel) { [self.testPanel removeFromSuperview]; self.testPanel = nil; return; }
    LCRemoteTestView *panel = [[LCRemoteTestView alloc] initWithFrame:CGRectInset(self.window.bounds, 12, 50)];
    panel.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    panel.backgroundColor = [UIColor colorWithRed:0.08 green:0.2 blue:0.35 alpha:1];
    UILabel *label = [[UILabel alloc] initWithFrame:CGRectMake(12, 12, panel.bounds.size.width - 24, 100)];
    label.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    label.textColor = UIColor.whiteColor; label.numberOfLines = 0;
    label.text = @"Touch test: click or drag the blue area. Drag the slider below.";
    [panel addSubview:label]; panel.status = label;
    UISlider *slider = [[UISlider alloc] initWithFrame:CGRectMake(16, 130, panel.bounds.size.width - 32, 50)];
    slider.autoresizingMask = UIViewAutoresizingFlexibleWidth; [panel addSubview:slider];
    [self.window addSubview:panel]; self.testPanel = panel;
}
- (UIView *)inputResponder:(UIView *)view {
    if (view.isFirstResponder && [view conformsToProtocol:@protocol(UIKeyInput)]) return view;
    for (UIView *child in view.subviews) {
        UIView *found = [self inputResponder:child];
        if (found) return found;
    }
    return nil;
}
- (BOOL)applyInput:(NSDictionary *)input {
    NSString *kind = input[@"kind"];
    if ([kind isEqual:@"test"]) { [self cancelTouch]; [self showInputTest]; return YES; }
    if ([kind isEqual:@"touch"]) {
        NSString *phase = input[@"phase"];
        if ([phase isEqual:@"cancelled"]) { [self cancelTouch]; return YES; }
        if (!input[@"x"] || !input[@"y"]) return NO;
        double x = [input[@"x"] doubleValue], y = [input[@"y"] doubleValue];
        if (!isfinite(x) || !isfinite(y) || x < 0 || x > 1 || y < 0 || y > 1) return NO;
        CGPoint point = CGPointMake(self.window.bounds.origin.x + x * self.window.bounds.size.width,
                                    self.window.bounds.origin.y + y * self.window.bounds.size.height);
        @try {
            UITouchPhase state;
            if ([phase isEqual:@"began"]) {
                [self cancelTouch];
                self.remoteTouch = LCRemoteBegin(self.window, point); state = UITouchPhaseBegan;
            } else if ([phase isEqual:@"moved"]) state = UITouchPhaseMoved;
            else if ([phase isEqual:@"ended"]) state = UITouchPhaseEnded;
            else return NO;
            if (!LCRemoteDeliver(self.remoteTouch, point, state)) { [self cancelTouch]; return NO; }
            [self.touchTimeout invalidate]; self.touchTimeout = nil;
            if (state == UITouchPhaseEnded) self.remoteTouch = nil;
            else self.touchTimeout = [NSTimer scheduledTimerWithTimeInterval:3 repeats:NO block:^(NSTimer *timer) { [self cancelTouch]; }];
            return YES;
        } @catch (NSException *exception) {
            NSLog(@"Guest touch unavailable: %@", exception.name); [self cancelTouch]; return NO;
        }
    }
    if ([kind isEqual:@"click"] || [kind isEqual:@"scroll"]) {
        double x = [input[@"x"] doubleValue], y = [input[@"y"] doubleValue];
        if (!isfinite(x) || !isfinite(y) || x < 0 || x > 1 || y < 0 || y > 1) return NO;
        UIView *hit = [self.window hitTest:CGPointMake(x * self.window.bounds.size.width, y * self.window.bounds.size.height) withEvent:nil];
        for (UIView *view = hit; view; view = view.superview) {
            if ([kind isEqual:@"scroll"] && [view isKindOfClass:UIScrollView.class]) {
                UIScrollView *scroll = (UIScrollView *)view;
                CGFloat dy = MAX(-500, MIN(500, [input[@"dy"] doubleValue]));
                CGFloat top = -scroll.adjustedContentInset.top;
                CGFloat bottom = MAX(top, scroll.contentSize.height - scroll.bounds.size.height + scroll.adjustedContentInset.bottom);
                [scroll setContentOffset:CGPointMake(scroll.contentOffset.x, MAX(top, MIN(bottom, scroll.contentOffset.y + dy))) animated:NO];
                return YES;
            }
            if ([kind isEqual:@"click"]) {
                if ([view isKindOfClass:UIControl.class] && ((UIControl *)view).enabled) {
                    [(UIControl *)view sendActionsForControlEvents:UIControlEventTouchUpInside];
                    return YES;
                }
                if (view.canBecomeFirstResponder) return [view becomeFirstResponder];
                if (view.isAccessibilityElement && [view accessibilityActivate]) return YES;
            }
        }
        return NO;
    }
    UIView *target = [self inputResponder:self.window];
    NSString *text = input[@"text"];
    if (!target || ![text isKindOfClass:NSString.class] || text.length > 1024) return NO;
    if ([input[@"key"] isEqual:@"Backspace"]) [(id<UIKeyInput>)target deleteBackward];
    else [(id<UIKeyInput>)target insertText:text];
    return YES;
}
- (instancetype)init {
    if ((self = [super init])) _listener = -1;
    return self;
}
- (void)install {
    // The recognizer is installed inside a guest, never on the host phone screen.
    self.discoveryTimer = [NSTimer scheduledTimerWithTimeInterval:1 repeats:YES block:^(NSTimer *timer) {
        for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
            if (![scene isKindOfClass:UIWindowScene.class]) continue;
            for (UIWindow *window in ((UIWindowScene *)scene).windows) {
                if (window.windowLevel != UIWindowLevelNormal) continue;
                if (objc_getAssociatedObject(window, @"LCDisplayProbeGesture")) continue;
                UILongPressGestureRecognizer *gesture = [[UILongPressGestureRecognizer alloc] initWithTarget:self action:@selector(toggle:)];
                gesture.numberOfTouchesRequired = 3;
                gesture.minimumPressDuration = 1.2;
                [window addGestureRecognizer:gesture];
                objc_setAssociatedObject(window, @"LCDisplayProbeGesture", gesture, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            }
        }
    }];
}
- (void)message:(NSString *)message onWindow:(UIWindow *)window {
    UIViewController *vc = window.rootViewController;
    while (vc.presentedViewController) vc = vc.presentedViewController;
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Guest display probe" message:message preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
    [vc presentViewController:alert animated:YES completion:nil];
}
- (void)stop {
    [self cancelTouch];
    [self.testPanel removeFromSuperview]; self.testPanel = nil;
    [self.captureTimer invalidate];
    self.captureTimer = nil;
    self.frame = nil;
    if (self.listener >= 0) {
        // The accept loop owns close(); shutdown wakes it without fd reuse races.
        shutdown(self.listener, SHUT_RDWR);
        self.listener = -1;
    }
}
- (void)toggle:(UILongPressGestureRecognizer *)gesture {
    if (gesture.state != UIGestureRecognizerStateBegan) return;
    UIWindow *window = (UIWindow *)gesture.view;
    if (self.listener >= 0) {
        [self stop];
        [self message:@"Sharing stopped." onWindow:window];
        return;
    }
    NSString *ip = nil;
    struct ifaddrs *interfaces = NULL;
    if (getifaddrs(&interfaces) == 0) {
        for (struct ifaddrs *item = interfaces; item; item = item->ifa_next) {
            if (!item->ifa_addr || item->ifa_addr->sa_family != AF_INET || strcmp(item->ifa_name, "en0")) continue;
            char address[INET_ADDRSTRLEN];
            if (inet_ntop(AF_INET, &((struct sockaddr_in *)item->ifa_addr)->sin_addr, address, sizeof(address))) ip = @(address);
        }
        freeifaddrs(interfaces);
    }
    if (!ip) { [self message:@"Connect the iPhone to Wi-Fi first." onWindow:window]; return; }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address = {0};
    address.sin_len = sizeof(address);
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip.UTF8String, &address.sin_addr);
    // OS-selected port: two guest processes cannot accidentally share a listener.
    if (fd < 0 || bind(fd, (struct sockaddr *)&address, sizeof(address)) || listen(fd, 4)) {
        if (fd >= 0) close(fd);
        [self message:@"Could not open the Wi-Fi listener." onWindow:window]; return;
    }
    socklen_t length = sizeof(address);
    if (getsockname(fd, (struct sockaddr *)&address, &length)) { close(fd); return; }
    NSString *token = [NSUUID.UUID.UUIDString stringByReplacingOccurrencesOfString:@"-" withString:@""];
    NSString *path = [@"/" stringByAppendingString:token];
    NSString *framePath = [path stringByAppendingString:@"/frame.jpg"];
    NSString *url = [NSString stringWithFormat:@"http://%@:%u%@", ip, ntohs(address.sin_port), path];
    self.listener = fd;
    self.window = window;
    self.sequence = 0;
    self.captureTimer = [NSTimer scheduledTimerWithTimeInterval:(1.0 / 15.0) repeats:YES block:^(NSTimer *timer) {
        UIWindow *guest = self.window;
        if (!guest || guest.bounds.size.width < 1 || guest.bounds.size.height < 1) return;
        UIGraphicsImageRendererFormat *format = [UIGraphicsImageRendererFormat defaultFormat];
        format.scale = MIN(1.0, 900.0 / MAX(guest.bounds.size.width, guest.bounds.size.height));
        format.opaque = YES;
        UIGraphicsImageRenderer *renderer = [[UIGraphicsImageRenderer alloc] initWithSize:guest.bounds.size format:format];
        __block BOOL rendered = NO;
        UIImage *image = [renderer imageWithActions:^(UIGraphicsImageRendererContext *context) {
            [UIColor.blackColor setFill]; UIRectFill(guest.bounds);
            rendered = [guest drawViewHierarchyInRect:guest.bounds afterScreenUpdates:NO];
            // Stamp is diagnostic only: it does NOT prove guest content is updating.
            NSString *stamp = [NSString stringWithFormat:@"Capture %lu", (unsigned long)++self.sequence];
            [stamp drawAtPoint:CGPointMake(8, 8) withAttributes:@{NSFontAttributeName: [UIFont systemFontOfSize:12], NSForegroundColorAttributeName: UIColor.redColor}];
        }];
        self.frame = rendered ? UIImageJPEGRepresentation(image, 0.65) : nil;
    }];
    [NSRunLoop.mainRunLoop addTimer:self.captureTimer forMode:NSRunLoopCommonModes];
    NSString *html = [NSString stringWithFormat:@"<!doctype html><meta name=\"viewport\" content=\"width=device-width\"><title>Guest display</title><style>body{background:#151515;color:white;font:16px system-ui;text-align:center}img{max-width:95vw;max-height:75vh;touch-action:none;user-select:none}input{width:65vw}button,input{padding:10px;margin:4px}#control{position:sticky;bottom:0;background:#222}</style><h1>Guest display</h1><p>15 FPS target · keep LiveContainer open</p><p id=\"s\">Connecting…</p><img id=\"f\" alt=\"Selected guest app\"><div id=\"control\"><input id=\"text\" maxlength=\"1024\" placeholder=\"Text or terminal command\"><button id=\"send\">Send text</button><button id=\"run\">Send + Enter</button><button id=\"back\">Backspace</button><button id=\"enter\">Enter</button><button id=\"test\">Show/hide touch test</button><label><input id=\"touch\" type=\"checkbox\" checked style=\"width:auto\">Experimental touch</label><p id=\"result\">Click or drag the guest. Untick touch to use basic controls.</p></div><script>const f=document.getElementById('f'),s=document.getElementById('s'),box=document.getElementById('text'),result=document.getElementById('result');let old,received=0,sampleStart=performance.now(),rate=0;async function tick(){const started=performance.now();let failed=false;try{const r=await fetch('%@',{cache:'no-store',signal:AbortSignal.timeout(4000)});if(!r.ok)throw Error(r.status);const u=URL.createObjectURL(await r.blob());f.src=u;if(old)URL.revokeObjectURL(old);old=u;received++;const now=performance.now();if(now-sampleStart>=2000){rate=received*1000/(now-sampleStart);received=0;sampleStart=now;}s.textContent='Receiving '+rate.toFixed(1)+' FPS · target 15';}catch(e){failed=true;s.textContent='No frame: '+e.message;}setTimeout(tick,failed?500:Math.max(0,1000/15-(performance.now()-started)))}tick();let queue=Promise.resolve();function send(data){queue=queue.catch(()=>{}).then(async()=>{try{const q=new URLSearchParams(data);const r=await fetch(location.pathname+'/input?'+q,{method:'POST',signal:AbortSignal.timeout(5000)});if(!r.ok)throw Error('Guest control unavailable ('+r.status+')');result.textContent='Input sent — verify the app responds';}catch(e){result.textContent=e.message;throw e;}});return queue;}document.getElementById('send').onclick=()=>send({text:box.value}).then(()=>box.value='').catch(()=>{});document.getElementById('run').onclick=()=>send({text:box.value+String.fromCharCode(10)}).then(()=>box.value='').catch(()=>{});document.getElementById('enter').onclick=()=>send({text:String.fromCharCode(10)}).catch(()=>{});document.getElementById('back').onclick=()=>send({text:'',key:'Backspace'}).catch(()=>{});box.onkeydown=e=>{if(e.key==='Enter'){e.preventDefault();document.getElementById('run').click();}};function pointer(kind,e,dy=0){const r=f.getBoundingClientRect();send({kind,x:(e.clientX-r.left)/r.width,y:(e.clientY-r.top)/r.height,dy}).catch(()=>{});}\nconst mode=document.getElementById('touch');\ndocument.getElementById('test').onclick=()=>send({kind:'test'}).catch(()=>{});\nlet active=null,lastMove=0;\nfunction coords(e){const r=f.getBoundingClientRect();return {x:Math.max(0,Math.min(1,(e.clientX-r.left)/r.width)),y:Math.max(0,Math.min(1,(e.clientY-r.top)/r.height))};}\nfunction touch(phase,e){return send({kind:'touch',phase,...(e?coords(e):{})}).catch(()=>{});}\nfunction cancel(){if(active===null)return;active=null;touch('cancelled');}\nf.addEventListener('pointerdown',e=>{if(e.button!==0)return;if(!mode.checked){pointer('click',e);return;}e.preventDefault();active=e.pointerId;lastMove=0;f.setPointerCapture(e.pointerId);touch('began',e);});\nf.addEventListener('pointermove',e=>{if(active!==e.pointerId||performance.now()-lastMove<80)return;lastMove=performance.now();touch('moved',e);});\nf.addEventListener('pointerup',e=>{if(active!==e.pointerId)return;active=null;touch('moved',e);touch('ended',e);});\nf.addEventListener('pointercancel',cancel);f.addEventListener('lostpointercapture',cancel);\nwindow.addEventListener('blur',cancel);document.addEventListener('visibilitychange',()=>{if(document.hidden)cancel();});mode.onchange=cancel;\nf.addEventListener('wheel',e=>{e.preventDefault();pointer('scroll',e,Math.max(-500,Math.min(500,e.deltaY)));},{passive:false});</script>", framePath];
    NSData *page = [html dataUsingEncoding:NSUTF8StringEncoding];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        for (;;) {
            int client = accept(fd, NULL, NULL);
            if (client < 0) { if (errno == EINTR) continue; break; }
            @autoreleasepool {
                int noSignal = 1;
                setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &noSignal, sizeof(noSignal));
                struct timeval timeout = {2, 0};
                setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
                setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
                NSMutableData *request = [NSMutableData data];
                NSString *line = nil;
                while (request.length < 16384 && !line) {
                    char buffer[512];
                    ssize_t n = recv(client, buffer, sizeof(buffer), 0);
                    if (n <= 0) break;
                    [request appendBytes:buffer length:(NSUInteger)n];
                    NSString *text = [[NSString alloc] initWithData:request encoding:NSUTF8StringEncoding];
                    if (request.length <= 16384 && [text containsString:@"\r\n"]) line = [text componentsSeparatedByString:@"\r\n"].firstObject;
                }
                NSArray *parts = [line componentsSeparatedByString:@" "];
                NSData *body = [NSData data]; NSString *type = @"text/plain"; int status = 404;
                if (parts.count == 3 && [parts[0] isEqual:@"GET"]) {
                    if ([parts[1] isEqual:path]) { body = page; type = @"text/html; charset=utf-8"; status = 200; }
                    else if ([parts[1] isEqual:framePath]) { NSData *snapshot = self.frame; if (snapshot) { body = snapshot; type = @"image/jpeg"; status = 200; } else status = 503; }
                }
                NSString *inputPrefix = [path stringByAppendingString:@"/input?"];
                if (parts.count == 3 && [parts[0] isEqual:@"POST"] && [parts[1] hasPrefix:inputPrefix]) {
                    // URLSearchParams uses form encoding: decode raw '+' as space,
                    // while preserving a literal plus encoded as %2B.
                    NSString *encodedTarget = [parts[1] stringByReplacingOccurrencesOfString:@"+" withString:@"%20"];
                    NSURLComponents *components = [NSURLComponents componentsWithString:[@"http://guest" stringByAppendingString:encodedTarget]];
                    NSMutableDictionary *input = [NSMutableDictionary dictionary];
                    for (NSURLQueryItem *item in components.queryItems) if (item.value) input[item.name] = item.value;
                    __block BOOL applied = NO;
                    dispatch_sync(dispatch_get_main_queue(), ^{ applied = [self applyInput:input]; });
                    status = applied ? 200 : 409;
                }
                NSString *header = [NSString stringWithFormat:@"HTTP/1.1 %d %@\r\nContent-Type: %@\r\nContent-Length: %lu\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n", status, status == 200 ? @"OK" : @"Unavailable", type, (unsigned long)body.length];
                if (LCProbeSend(client, [header dataUsingEncoding:NSUTF8StringEncoding])) LCProbeSend(client, body);
                close(client);
            }
        }
        close(fd);
    });
    [self message:[NSString stringWithFormat:@"Open this address on your PC on the same network:\n\n%@\n\nThree-finger hold again to stop. Anyone with this address on your network can view this guest. HTTP is unencrypted. This is a capture experiment, with experimental remote input enabled.", url] onWindow:window];
}
@end
