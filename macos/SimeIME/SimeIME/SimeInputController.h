//
//  SimeInputController.h
//  SimeIME
//
//  Input Method Controller for macOS
//

#import <InputMethodKit/InputMethodKit.h>

@class SimeCandidate;

NS_ASSUME_NONNULL_BEGIN

@class SimeCandidatesWindow;

@interface SimeInputController : IMKInputController

@property(nonatomic, readonly) NSString *currentPinyin;
@property(nonatomic, readonly) NSArray<SimeCandidate *> *candidates;
@property(nonatomic, readonly) NSInteger selectedIndex;

/// Commit the selected candidate
- (void)commitSelectedCandidate;

/// Commit the given text to the client
- (void)commitText:(NSString *)text;

/// Cancel current input session
- (void)cancelInput;

/// Handle candidate selection by number key
- (BOOL)handleNumberKey:(unichar)ch;

/// Handle page up/down
- (BOOL)handlePageUp;
- (BOOL)handlePageDown;

/// Handle cursor movement
- (void)selectNextCandidate;
- (void)selectPreviousCandidate;

@end

NS_ASSUME_NONNULL_END
