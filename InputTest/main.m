#import <UIKit/UIKit.h>

@interface TestController : UIViewController
@property(nonatomic,strong) UILabel *counterLabel;
@property(nonatomic,strong) UILabel *sliderLabel;
@property(nonatomic,strong) UITextField *field;
@property(nonatomic) NSInteger count;
@end
@implementation TestController
- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"PC Control Test";
    self.view.backgroundColor = UIColor.systemBackgroundColor;
    UIScrollView *scroll = [UIScrollView new];
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.keyboardDismissMode = UIScrollViewKeyboardDismissModeOnDrag;
    [self.view addSubview:scroll];
    UIStackView *stack = [UIStackView new];
    stack.axis = UILayoutConstraintAxisVertical; stack.spacing = 20;
    stack.translatesAutoresizingMaskIntoConstraints = NO; [scroll addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[
        [scroll.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor],
        [scroll.bottomAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.bottomAnchor],
        [scroll.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [scroll.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [stack.topAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.topAnchor constant:24],
        [stack.bottomAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.bottomAnchor constant:-24],
        [stack.leadingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.leadingAnchor constant:24],
        [stack.trailingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.trailingAnchor constant:-24],
        [stack.widthAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.widthAnchor constant:-48]
    ]];
    UILabel *intro = [UILabel new]; intro.numberOfLines = 0;
    intro.text = @"Hold three fingers here to start sharing. On the PC, try the controls below.";
    [stack addArrangedSubview:intro];
    self.counterLabel = [UILabel new]; self.counterLabel.text = @"Button taps: 0";
    self.counterLabel.font = [UIFont boldSystemFontOfSize:24]; [stack addArrangedSubview:self.counterLabel];
    UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
    [button setTitle:@"Tap to add one" forState:UIControlStateNormal];
    [button addTarget:self action:@selector(increment) forControlEvents:UIControlEventTouchUpInside];
    [button.heightAnchor constraintEqualToConstant:52].active = YES; [stack addArrangedSubview:button];
    self.field = [UITextField new]; self.field.borderStyle = UITextBorderStyleRoundedRect;
    self.field.placeholder = @"Click here, then send text from PC";
    self.field.autocorrectionType = UITextAutocorrectionTypeNo;
    [self.field.heightAnchor constraintEqualToConstant:48].active = YES; [stack addArrangedSubview:self.field];
    self.sliderLabel = [UILabel new]; self.sliderLabel.text = @"Slider: 0%"; [stack addArrangedSubview:self.sliderLabel];
    UISlider *slider = [UISlider new];
    [slider addTarget:self action:@selector(sliderChanged:) forControlEvents:UIControlEventValueChanged];
    [slider.heightAnchor constraintEqualToConstant:50].active = YES; [stack addArrangedSubview:slider];
    UISwitch *toggle = [UISwitch new]; [stack addArrangedSubview:toggle];
    UILabel *instructions = [UILabel new]; instructions.text = @"Scroll down to find the green finish box.";
    instructions.numberOfLines = 0; [stack addArrangedSubview:instructions];
    for (NSUInteger i=1; i<=12; i++) {
        UILabel *row = [UILabel new]; row.text = [NSString stringWithFormat:@"Scroll marker %lu", (unsigned long)i];
        [row.heightAnchor constraintEqualToConstant:64].active = YES; [stack addArrangedSubview:row];
    }
    UILabel *finish = [UILabel new]; finish.text = @"SCROLL TEST FINISHED"; finish.textAlignment = NSTextAlignmentCenter;
    finish.backgroundColor = UIColor.systemGreenColor; finish.textColor = UIColor.blackColor;
    [finish.heightAnchor constraintEqualToConstant:80].active = YES; [stack addArrangedSubview:finish];
}
- (void)increment { self.counterLabel.text = [NSString stringWithFormat:@"Button taps: %ld", (long)++self.count]; }
- (void)sliderChanged:(UISlider *)slider { self.sliderLabel.text = [NSString stringWithFormat:@"Slider: %.0f%%", slider.value*100]; }
@end

@interface TestDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic,strong) UIWindow *window;
@end
@implementation TestDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options {
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = [[UINavigationController alloc] initWithRootViewController:[TestController new]];
    [self.window makeKeyAndVisible]; return YES;
}
@end
int main(int argc, char **argv) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(TestDelegate.class)); }
}
