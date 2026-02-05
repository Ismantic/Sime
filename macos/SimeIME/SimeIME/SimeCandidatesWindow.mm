//
//  SimeCandidatesWindow.mm
//  SimeIME
//
//  Candidate window implementation
//

#import "SimeCandidatesWindow.h"
#import "SimeEngine.h"

// MARK: - SimeCandidateView

@interface SimeCandidateView : NSView

@property(nonatomic, strong) NSArray<SimeCandidate *> *candidates;
@property(nonatomic, assign) NSInteger selectedIndex;

- (void)updateCandidates:(NSArray<SimeCandidate *> *)candidates
           selectedIndex:(NSInteger)selectedIndex;
- (CGFloat)preferredHeight;

@end

@implementation SimeCandidateView {
    NSTextField *_pinyinLabel;
    NSMutableArray<NSTextField *> *_candidateLabels;
}

- (instancetype)initWithFrame:(NSRect)frameRect {
    self = [super initWithFrame:frameRect];
    if (self) {
        _candidates = @[];
        _selectedIndex = 0;
        _candidateLabels = [NSMutableArray array];

        self.wantsLayer = YES;

        // Use a VERY visible background color (bright yellow for testing)
        self.layer.backgroundColor = [[NSColor colorWithRed:1.0 green:1.0 blue:0.0 alpha:1.0] CGColor];

        // Add a VERY prominent border
        self.layer.borderColor = [[NSColor redColor] CGColor];
        self.layer.borderWidth = 5.0;
        self.layer.cornerRadius = 6.0;

        // Add strong shadow for depth
        self.layer.shadowColor = [[NSColor blackColor] CGColor];
        self.layer.shadowOpacity = 0.8;
        self.layer.shadowOffset = NSMakeSize(0, -4);
        self.layer.shadowRadius = 8.0;

        // Pinyin label at top
        _pinyinLabel = [self createLabel];
        _pinyinLabel.font = [NSFont systemFontOfSize:12 weight:NSFontWeightMedium];
        _pinyinLabel.textColor = [NSColor systemBlueColor];
        [self addSubview:_pinyinLabel];

        NSLog(@"[SimeCandidateView] View initialized with white background");
    }
    return self;
}

- (NSTextField *)createLabel {
    NSTextField *label = [[NSTextField alloc] init];
    label.editable = NO;
    label.bordered = NO;
    label.backgroundColor = [NSColor clearColor];
    label.font = [NSFont systemFontOfSize:16];
    label.lineBreakMode = NSLineBreakByClipping;
    [label setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                      forOrientation:NSLayoutConstraintOrientationHorizontal];
    return label;
}

- (void)updateCandidates:(NSArray<SimeCandidate *> *)candidates
           selectedIndex:(NSInteger)selectedIndex {
    NSLog(@"[SimeCandidateView] updateCandidates: candidates=%lu, selectedIndex=%ld", (unsigned long)candidates.count, (long)selectedIndex);
    self.candidates = candidates;
    self.selectedIndex = selectedIndex;

    // Update pinyin label if we have candidates
    if (candidates.count > 0) {
        _pinyinLabel.stringValue = [NSString stringWithFormat:@"%lu candidates", (unsigned long)candidates.count];
    } else {
        _pinyinLabel.stringValue = @"";
    }

    // Safety check: ensure _candidateLabels is a valid NSMutableArray
    if (!_candidateLabels || ![_candidateLabels isKindOfClass:[NSMutableArray class]]) {
        NSLog(@"[SimeCandidateView] Reinitializing _candidateLabels");
        _candidateLabels = [NSMutableArray array];
    }

    // Verify all existing elements are NSTextFields, clear if corrupted
    for (NSUInteger i = 0; i < _candidateLabels.count; i++) {
        id obj = _candidateLabels[i];
        if (![obj isKindOfClass:[NSTextField class]]) {
            NSLog(@"[SimeCandidateView] Corrupted element at index %lu: %@, clearing array", (unsigned long)i, [obj class]);
            // Remove all subviews and clear array
            for (NSView *subview in [self.subviews copy]) {
                if (subview != _pinyinLabel) {
                    [subview removeFromSuperview];
                }
            }
            [_candidateLabels removeAllObjects];
            break;
        }
    }

    // Ensure we have enough labels
    while (_candidateLabels.count < candidates.count) {
        @try {
            NSTextField *label = [self createLabel];
            [_candidateLabels addObject:label];
            [self addSubview:label];
        } @catch (NSException *exception) {
            NSLog(@"[SimeCandidateView] Exception creating label: %@", exception);
            _candidateLabels = [NSMutableArray array];
            break;
        }
    }

    // Hide unused labels - with additional safety check
    for (NSUInteger i = candidates.count; i < _candidateLabels.count; i++) {
        id label = _candidateLabels[i];
        if ([label isKindOfClass:[NSTextField class]]) {
            [(NSTextField *)label setHidden:YES];
        }
    }

    // FIRST: Clear all highlights to ensure clean state
    for (NSUInteger i = 0; i < _candidateLabels.count; i++) {
        id obj = _candidateLabels[i];
        if ([obj isKindOfClass:[NSTextField class]]) {
            NSTextField *label = (NSTextField *)obj;
            label.drawsBackground = NO;
            label.backgroundColor = [NSColor clearColor];
            label.textColor = [NSColor blackColor];
        }
    }

    // THEN: Update candidate labels with horizontal style
    for (NSUInteger i = 0; i < candidates.count; i++) {
        SimeCandidate *candidate = candidates[i];

        // Safety check: ensure element is NSTextField
        if (i >= _candidateLabels.count) {
            NSLog(@"[SimeCandidateView] ERROR: Index %lu out of bounds (count=%lu)", (unsigned long)i, (unsigned long)_candidateLabels.count);
            break;
        }

        id obj = _candidateLabels[i];
        if (![obj isKindOfClass:[NSTextField class]]) {
            NSLog(@"[SimeCandidateView] ERROR: Element %lu is not NSTextField: %@", (unsigned long)i, [obj class]);
            continue;
        }

        NSTextField *label = (NSTextField *)obj;
        label.hidden = NO;

        NSString *number = [NSString stringWithFormat:@"%lu.", (unsigned long)(i + 1)];
        label.stringValue = [NSString stringWithFormat:@"%@ %@", number, candidate.text];

        // Highlight ONLY the selected candidate
        if ((NSInteger)i == selectedIndex) {
            NSLog(@"[SimeCandidateView] Setting highlight on candidate %lu", (unsigned long)i);
            label.drawsBackground = YES;  // CRITICAL: Enable background drawing
            label.backgroundColor = [NSColor colorWithRed:0.0 green:0.5 blue:1.0 alpha:0.8]; // Blue highlight
            label.textColor = [NSColor whiteColor];
        }
    }

    // VERIFY: Log final state of all labels
    NSMutableString *stateLog = [NSMutableString stringWithFormat:@"[SimeCandidateView] Final state for selectedIndex=%ld: ", (long)selectedIndex];
    for (NSUInteger i = 0; i < candidates.count && i < _candidateLabels.count; i++) {
        id obj = _candidateLabels[i];
        if ([obj isKindOfClass:[NSTextField class]]) {
            NSTextField *label = (NSTextField *)obj;
            [stateLog appendFormat:@"[%lu:%@] ", (unsigned long)i, label.drawsBackground ? @"★" : @"☐"];
        }
    }
    NSLog(@"%@", stateLog);

    [self setNeedsLayout:YES];
    [self setNeedsDisplay:YES];

    // Force immediate redraw
    [self displayIfNeeded];
    NSLog(@"[SimeCandidateView] View updated and redrawn");
}

- (CGFloat)preferredHeight {
    // For horizontal layout, we only need one row height
    CGFloat rowHeight = 36.0;
    CGFloat padding = 16.0;
    return rowHeight + padding;
}

- (CGFloat)preferredWidth {
    // Calculate width based on number of candidates
    // Each candidate roughly: number(20) + text(60) = 80 pixels
    CGFloat candidateWidth = 80.0;
    CGFloat margin = 16.0;
    return (_candidates.count * candidateWidth) + margin * 2;
}

- (void)layout {
    [super layout];

    NSRect bounds = self.bounds;
    CGFloat margin = 8.0;
    CGFloat candidateHeight = 28.0;
    CGFloat candidateWidth = (bounds.size.width - margin * 2) / MAX(1, _candidates.count);

    // Hide pinyin label for now (or we can show it at the top)
    _pinyinLabel.hidden = YES;

    // Layout candidates horizontally
    CGFloat x = margin;
    CGFloat y = (bounds.size.height - candidateHeight) / 2; // Center vertically

    for (NSUInteger i = 0; i < _candidates.count; i++) {
        _candidateLabels[i].frame = NSMakeRect(x, y, candidateWidth - 4, candidateHeight);
        x += candidateWidth;
    }
}

@end

// MARK: - SimeCandidatesWindow

@interface SimeCandidatesWindow ()
@property(nonatomic, strong) SimeCandidateView *candidateView;
@end

@implementation SimeCandidatesWindow

- (instancetype)init {
    NSRect contentRect = NSMakeRect(0, 0, 200, 280);

    self = [super initWithContentRect:contentRect
                            styleMask:NSWindowStyleMaskBorderless
                              backing:NSBackingStoreBuffered
                                defer:NO];
    if (self) {
        // Set to MAXIMUM window level to ensure visibility
        self.level = NSScreenSaverWindowLevel;
        self.hasShadow = YES;

        // Make the window opaque with a VERY visible background
        self.opaque = YES;
        self.backgroundColor = [NSColor colorWithRed:1.0 green:0.9 blue:0.9 alpha:1.0];

        // Make sure window appears on all spaces and above everything
        self.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
                                   NSWindowCollectionBehaviorStationary |
                                   NSWindowCollectionBehaviorFullScreenAuxiliary;

        // Ensure window is not hidden
        self.ignoresMouseEvents = NO;
        self.hidesOnDeactivate = NO;

        _candidateView = [[SimeCandidateView alloc] initWithFrame:contentRect];
        self.contentView = _candidateView;

        NSLog(@"[SimeCandidatesWindow] Window initialized with level: %ld", (long)self.level);
    }
    return self;
}

- (void)updateCandidates:(NSArray<SimeCandidate *> *)candidates
           selectedIndex:(NSInteger)selectedIndex {
    NSLog(@"[SimeCandidatesWindow] updateCandidates called with selectedIndex: %ld, isVisible before: %@",
          (long)selectedIndex, self.isVisible ? @"YES" : @"NO");

    [self.candidateView updateCandidates:candidates selectedIndex:selectedIndex];

    // Update window size for horizontal layout
    CGFloat preferredHeight = [self.candidateView preferredHeight];
    CGFloat preferredWidth = [self.candidateView preferredWidth];

    NSRect frame = self.frame;
    NSLog(@"[SimeCandidatesWindow] Old frame: (%.1f, %.1f) size: (%.1f x %.1f)",
          frame.origin.x, frame.origin.y, frame.size.width, frame.size.height);

    frame.size = NSMakeSize(preferredWidth, preferredHeight);
    NSLog(@"[SimeCandidatesWindow] New frame: (%.1f, %.1f) size: (%.1f x %.1f)",
          frame.origin.x, frame.origin.y, frame.size.width, frame.size.height);

    [self setFrame:frame display:YES animate:NO];

    NSLog(@"[SimeCandidatesWindow] After setFrame, isVisible: %@", self.isVisible ? @"YES" : @"NO");

    // CRITICAL: Always re-assert visibility after frame change
    [self orderFrontRegardless];
    NSLog(@"[SimeCandidatesWindow] After orderFrontRegardless, isVisible: %@", self.isVisible ? @"YES" : @"NO");
}

- (void)showAtCaretPosition:(NSPoint)position {
    NSLog(@"[SimeCandidatesWindow] showAtCaretPosition called: (%.1f, %.1f)", position.x, position.y);
    NSRect frame = self.frame;
    NSLog(@"[SimeCandidatesWindow] Window size: %.1f x %.1f", frame.size.width, frame.size.height);

    // Get screen dimensions
    NSScreen *screen = [NSScreen mainScreen];
    if (!screen) {
        screen = [NSScreen screens].firstObject;
    }
    NSRect screenFrame = screen.visibleFrame;
    NSLog(@"[SimeCandidatesWindow] Screen frame: origin=(%.1f, %.1f) size=(%.1f x %.1f)",
          screenFrame.origin.x, screenFrame.origin.y, screenFrame.size.width, screenFrame.size.height);

    // TESTING: Show in center of screen for maximum visibility
    frame.origin.x = screenFrame.origin.x + (screenFrame.size.width - frame.size.width) / 2;
    frame.origin.y = screenFrame.origin.y + (screenFrame.size.height - frame.size.height) / 2;

    NSLog(@"[SimeCandidatesWindow] TEST MODE: Showing in CENTER of screen at (%.1f, %.1f)", frame.origin.x, frame.origin.y);

    [self setFrame:frame display:YES animate:NO];

    // Force window to be visible with MAXIMUM priority
    [self orderFrontRegardless];
    [self setAlphaValue:1.0];

    // Force a display update
    [self display];
    [self.contentView setNeedsDisplay:YES];

    NSLog(@"[SimeCandidatesWindow] orderFrontRegardless called");
    NSLog(@"[SimeCandidatesWindow] Window level: %ld (NSScreenSaverWindowLevel=%ld)", (long)self.level, (long)NSScreenSaverWindowLevel);
    NSLog(@"[SimeCandidatesWindow] Window isVisible: %@", self.isVisible ? @"YES" : @"NO");
    NSLog(@"[SimeCandidatesWindow] Window isKeyWindow: %@", self.isKeyWindow ? @"YES" : @"NO");
    NSLog(@"[SimeCandidatesWindow] Window alpha: %.2f", self.alphaValue);
    NSLog(@"[SimeCandidatesWindow] Window frame: origin=(%.1f, %.1f) size=(%.1f x %.1f)",
          self.frame.origin.x, self.frame.origin.y, self.frame.size.width, self.frame.size.height);
    NSLog(@"[SimeCandidatesWindow] Window screen: %@", self.screen ? @"YES" : @"NO");
}

@end
