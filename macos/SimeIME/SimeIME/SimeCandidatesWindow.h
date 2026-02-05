//
//  SimeCandidatesWindow.h
//  SimeIME
//
//  Candidate window for displaying pinyin conversion results
//

#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

@class SimeCandidate;

@interface SimeCandidatesWindow : NSPanel

/// Update the candidates display
- (void)updateCandidates:(NSArray<SimeCandidate *> *)candidates
           selectedIndex:(NSInteger)selectedIndex;

/// Show the window at the specified screen position
- (void)showAtCaretPosition:(NSPoint)position;

@end

NS_ASSUME_NONNULL_END
