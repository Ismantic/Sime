//
//  SimeInputController.mm
//  SimeIME
//
//  Input Method Controller implementation
//

#import "SimeInputController.h"
#import "SimeEngine.h"
#import "SimeCandidatesWindow.h"

#import <InputMethodKit/InputMethodKit.h>
#include "selection_manager.h"

// Maximum number of candidates to fetch from engine
static const NSUInteger kMaxCandidates = 100;

// Page size for candidate window
static const NSUInteger kPageSize = 9;

@interface SimeInputController () {
    NSString *_currentPinyin;
    NSArray<SimeCandidate *> *_candidates;
    NSInteger _selectedIndex;
    SimeCandidatesWindow *_candidatesWindow;
    BOOL _active;
    std::unique_ptr<sime::SelectionManager> _selectionManager;
    NSInteger _currentPage;  // 当前页码（0-based）
}
@end

@implementation SimeInputController

@synthesize currentPinyin = _currentPinyin;
// Note: _candidates is NSArray<SimeCandidate *> *, exposed as NSArray *
@synthesize selectedIndex = _selectedIndex;

- (instancetype)initWithServer:(IMKServer *)server delegate:(id)delegate client:(id)client {
    self = [super initWithServer:server delegate:delegate client:client];
    if (self) {
        NSLog(@"[SimeInputController] init called for client: %@", client);
        _currentPinyin = @"";
        _candidates = @[];
        _selectedIndex = 0;
        _currentPage = 0;
        _active = NO;
        _selectionManager = std::make_unique<sime::SelectionManager>();

        // Ensure engine is loaded
        [[SimeEngine sharedEngine] loadResources];
    }
    return self;
}

- (void)dealloc {
    [_candidatesWindow close];
}

#pragma mark - IMKInputController Text Handling

- (NSString *)composedString:(id)sender {
    // This method is crucial - IMKit uses it to know if there's active composition
    NSLog(@"[SimeInputController] composedString called, returning: %@", _currentPinyin.length > 0 ? _currentPinyin : @"(nil)");
    if (_currentPinyin.length > 0) {
        return _currentPinyin;
    }
    return nil;
}

- (NSString *)originalString:(id)sender {
    // Return the original input string
    return _currentPinyin.length > 0 ? _currentPinyin : nil;
}

- (NSAttributedString *)attributedSubstringFromRange:(NSRange)range client:(id)sender {
    // Return attributed string for the marked text
    if (_currentPinyin.length > 0 && range.location == 0) {
        NSMutableAttributedString *attrString = [[NSMutableAttributedString alloc] initWithString:_currentPinyin];

        // Add underline to show it's being composed
        [attrString addAttribute:NSUnderlineStyleAttributeName
                          value:@(NSUnderlineStyleSingle)
                          range:NSMakeRange(0, _currentPinyin.length)];

        return attrString;
    }
    return nil;
}

#pragma mark - IMKState Protocol

- (void)activateServer:(id)client {
    _active = YES;
    NSLog(@"[SimeInputController] ACTIVATED!");
    NSLog(@"[SimeInputController] Client: %@", client);
    NSLog(@"[SimeInputController] Client bundle ID: %@", [client respondsToSelector:@selector(bundleIdentifier)] ? [client bundleIdentifier] : @"N/A");

    // Ensure engine is loaded
    if (![[SimeEngine sharedEngine] isReady]) {
        NSLog(@"[SimeInputController] Loading engine...");
        [[SimeEngine sharedEngine] loadResources];
    }

    NSLog(@"[SimeInputController] Engine ready: %@", [[SimeEngine sharedEngine] isReady] ? @"YES" : @"NO");

    // Show candidates window if we have candidates
    if (_candidates.count > 0) {
        [self showCandidatesWindow];
    }
}

- (void)deactivateServer:(id)client {
    _active = NO;
    [self hideCandidatesWindow];
    
    // Commit any pending input
    if (_currentPinyin.length > 0) {
        [self commitText:_currentPinyin];
    }
}

#pragma mark - IMKInputController Overrides

- (NSUInteger)recognizedEvents:(id)sender {
    // Tell the system which events we want to handle
    // NSKeyDownMask is essential for receiving keyboard input
    NSLog(@"[SimeInputController] recognizedEvents called, returning: %lu", (unsigned long)(NSEventMaskKeyDown | NSEventMaskFlagsChanged));
    return NSEventMaskKeyDown | NSEventMaskFlagsChanged;
}

- (BOOL)inputText:(NSString *)string client:(id)sender {
    // This method is called for direct text input
    // Return YES if we handle it, NO to let system handle it
    NSLog(@"[SimeInputController] inputText called: %@", string);

    if (string.length == 0) {
        return NO;
    }

    // Check if it's a valid pinyin character
    unichar ch = [string characterAtIndex:0];
    if ([SimeEngine isValidPinyinChar:ch]) {
        [self appendPinyinCharacter:ch];
        return YES;
    }

    // For other input, commit any pending pinyin first
    if (_currentPinyin.length > 0) {
        [self commitText:_currentPinyin];
    }

    return NO;
}

- (BOOL)handleEvent:(NSEvent *)event client:(id)sender {
    NSLog(@"[SimeInputController] handleEvent called, type: %lu", (unsigned long)event.type);
    
    if (![[SimeEngine sharedEngine] isReady]) {
        NSLog(@"[SimeInputController] Engine not ready");
        return NO;
    }
    
    // Only handle key down events
    if (event.type != NSEventTypeKeyDown) {
        return NO;
    }
    
    NSUInteger modifierFlags = event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask;
    NSString *characters = event.characters;
    
    if (characters.length == 0) {
        return NO;
    }
    
    unichar ch = [characters characterAtIndex:0];
    NSLog(@"[SimeInputController] Character received: %C (0x%04X), modifiers: %lu", ch, ch, modifierFlags);
    
    // Handle special keys
    switch (ch) {
        case NSDeleteFunctionKey:
        case NSDeleteCharFunctionKey:
        case 0x7F:  // Delete key
            return [self handleDeleteKey];
            
        case 0x0003:  // Enter
        case 0x0D:  // Return
            NSLog(@"[SimeInputController] Return key pressed, candidates: %lu", (unsigned long)_candidates.count);
            if (_candidates.count > 0) {
                [self commitSelectedCandidate];
                return YES;
            } else if (_currentPinyin.length > 0) {
                [self commitText:_currentPinyin];
                return YES;
            }
            return NO;
            
        case 0x001B:  // Escape
            [self cancelInput];
            return YES;
            
        case NSLeftArrowFunctionKey:
            if (_candidates.count > 0) {
                // 如果在当前页的第一个候选，尝试翻到上一页
                NSInteger pageStartIndex = _currentPage * kPageSize;
                if (_selectedIndex == pageStartIndex && _currentPage > 0) {
                    [self goToPreviousPage];
                } else {
                    [self selectPreviousCandidate];
                }
                return YES;
            }
            return NO;

        case NSRightArrowFunctionKey:
            if (_candidates.count > 0) {
                // 如果在当前页的最后一个候选，尝试翻到下一页
                NSInteger pageEndIndex = MIN((_currentPage + 1) * kPageSize - 1, (NSInteger)_candidates.count - 1);
                if (_selectedIndex == pageEndIndex && (_currentPage + 1) * kPageSize < (NSInteger)_candidates.count) {
                    [self goToNextPage];
                } else {
                    [self selectNextCandidate];
                }
                return YES;
            }
            return NO;

        case NSUpArrowFunctionKey:
            NSLog(@"[SimeInputController] Up arrow pressed");
            if (_candidates.count > 0) {
                // 上箭头：翻到上一页
                if (_currentPage > 0) {
                    [self goToPreviousPage];
                } else {
                    [self selectPreviousCandidate];
                }
                return YES;
            }
            return NO;

        case NSDownArrowFunctionKey:
            NSLog(@"[SimeInputController] Down arrow pressed");
            if (_candidates.count > 0) {
                // 下箭头：翻到下一页
                if ((_currentPage + 1) * kPageSize < (NSInteger)_candidates.count) {
                    [self goToNextPage];
                } else {
                    [self selectNextCandidate];
                }
                return YES;
            }
            return NO;
            
        case NSPageUpFunctionKey:
            return [self handlePageUp];
            
        case NSPageDownFunctionKey:
            return [self handlePageDown];
            
        case ' ':
            if (_candidates.count > 0) {
                [self commitSelectedCandidate];
                return YES;
            }
            return NO;
            
        case '\t':
            if (_candidates.count > 0) {
                [self selectNextCandidate];
                return YES;
            }
            return NO;
    }
    
    // Handle number keys for candidate selection (1-9)
    if (ch >= '1' && ch <= '9') {
        if ([self handleNumberKey:ch]) {
            return YES;
        }
    }
    
    // Handle pinyin input
    if ([SimeEngine isValidPinyinChar:ch]) {
        NSLog(@"[SimeInputController] Valid pinyin char: %C", ch);
        [self appendPinyinCharacter:ch];
        return YES;
    }
    
    // For other characters, commit current pinyin and pass through
    if (_currentPinyin.length > 0) {
        [self commitText:_currentPinyin];
    }
    
    return NO;
}

- (BOOL)handleDeleteKey {
    // 优先级 1: 拼音缓冲区不为空，删除最后一个字符
    if (_currentPinyin.length > 0) {
        _selectionManager->DeleteLastPinyin();
        _currentPinyin = [NSString stringWithUTF8String:_selectionManager->GetPinyinBuffer().c_str()];

        // Update the marked text
        id client = [self client];
        if (_currentPinyin.length > 0) {
            if (client && [client respondsToSelector:@selector(setMarkedText:selectionRange:replacementRange:)]) {
                NSMutableAttributedString *markedText = [[NSMutableAttributedString alloc] initWithString:_currentPinyin];
                [markedText addAttribute:NSUnderlineStyleAttributeName
                                  value:@(NSUnderlineStyleSingle)
                                  range:NSMakeRange(0, _currentPinyin.length)];
                [client setMarkedText:markedText
                       selectionRange:NSMakeRange(_currentPinyin.length, 0)
                  replacementRange:NSMakeRange(NSNotFound, NSNotFound)];
            }
        } else {
            // Clear marked text if empty
            if (client && [client respondsToSelector:@selector(setMarkedText:selectionRange:replacementRange:)]) {
                [client setMarkedText:@""
                       selectionRange:NSMakeRange(0, 0)
                  replacementRange:NSMakeRange(NSNotFound, NSNotFound)];
            }
        }

        [self updateCandidates];
        return YES;
    }

    // 优先级 2: 拼音缓冲区为空但有选择历史，撤销最后一次选择
    if (_selectionManager->HasSelections()) {
        if (_selectionManager->UndoLastSelection()) {
            _currentPinyin = [NSString stringWithUTF8String:_selectionManager->GetPinyinBuffer().c_str()];
            [self updateCandidates];

            // Update marked text
            id client = [self client];
            if (client && [client respondsToSelector:@selector(setMarkedText:selectionRange:replacementRange:)]) {
                NSMutableAttributedString *markedText = [[NSMutableAttributedString alloc] initWithString:_currentPinyin];
                [markedText addAttribute:NSUnderlineStyleAttributeName
                                  value:@(NSUnderlineStyleSingle)
                                  range:NSMakeRange(0, _currentPinyin.length)];
                [client setMarkedText:markedText
                       selectionRange:NSMakeRange(_currentPinyin.length, 0)
                  replacementRange:NSMakeRange(NSNotFound, NSNotFound)];
            }
            return YES;
        }
    }

    // 优先级 3: 都为空，不处理（传递给应用程序）
    return NO;
}

- (void)appendPinyinCharacter:(unichar)ch {
    // 使用 SelectionManager 添加拼音字符
    _selectionManager->AppendPinyin(static_cast<char>(ch));
    _currentPinyin = [NSString stringWithUTF8String:_selectionManager->GetPinyinBuffer().c_str()];
    _selectedIndex = 0;

    // Update the marked text
    id client = [self client];
    if (client) {
        // 构建显示文本：已选文字 + 当前拼音
        NSMutableAttributedString *markedText = [[NSMutableAttributedString alloc] init];

        // 第一部分：已选文字（加粗+下划线）
        if (_selectionManager->HasSelections()) {
            std::string committedText = _selectionManager->GetCommittedText();
            NSString *committedStr = [NSString stringWithUTF8String:committedText.c_str()];
            NSMutableAttributedString *committedPart = [[NSMutableAttributedString alloc] initWithString:committedStr];
            [committedPart addAttribute:NSUnderlineStyleAttributeName
                                 value:@(NSUnderlineStyleSingle)
                                 range:NSMakeRange(0, committedStr.length)];
            [committedPart addAttribute:NSFontAttributeName
                                 value:[NSFont boldSystemFontOfSize:[NSFont systemFontSize]]
                                 range:NSMakeRange(0, committedStr.length)];
            [markedText appendAttributedString:committedPart];
        }

        // 第二部分：当前拼音（普通样式）
        if (_currentPinyin.length > 0) {
            NSAttributedString *pinyinPart = [[NSAttributedString alloc] initWithString:_currentPinyin];
            [markedText appendAttributedString:pinyinPart];
        }

        [client setMarkedText:markedText
               selectionRange:NSMakeRange(markedText.length, 0)
          replacementRange:NSMakeRange(NSNotFound, NSNotFound)];
    }

    [self updateCandidates];
}

- (void)updateCandidates {
    NSLog(@"[SimeInputController] updateCandidates called, pinyin: %@", _currentPinyin);

    if (_currentPinyin.length == 0) {
        _candidates = @[];
        _selectionManager->ClearCandidates();
        _currentPage = 0;
        _selectedIndex = 0;
        [self hideCandidatesWindow];
        return;
    }

    // 检查缓存
    std::string currentPinyinStr([_currentPinyin UTF8String]);
    if (currentPinyinStr == _selectionManager->GetCachedPinyin() &&
        !_selectionManager->GetCandidates().empty()) {
        // 使用缓存的候选词
        const auto& cachedCandidates = _selectionManager->GetCandidates();
        NSMutableArray<SimeCandidate *> *candidatesArray = [NSMutableArray array];
        for (size_t i = 0; i < cachedCandidates.size(); i++) {
            const auto& cand = cachedCandidates[i];
            NSString *text = [NSString stringWithUTF8String:cand.text.c_str()];
            SimeCandidate *candidate = [[SimeCandidate alloc] initWithText:text
                                                                      score:cand.score
                                                                      index:i
                                                              matchedLength:cand.matched_length];
            [candidatesArray addObject:candidate];
        }
        _candidates = [candidatesArray copy];

        // 重要：使用缓存时也需要重置页码和选中索引
        // 因为拼音可能已经改变（即使使用缓存）
        _currentPage = 0;
        _selectedIndex = 0;

        [self showCandidatesWindow];
        return;
    }

    // 生成新的候选词 - 重置页码和选中索引
    _currentPage = 0;
    _selectedIndex = 0;
    _candidates = [[[SimeEngine sharedEngine] decodePinyin:_currentPinyin nbest:kMaxCandidates] copy];

    NSLog(@"[SimeInputController] Got %lu candidates", (unsigned long)_candidates.count);
    for (NSUInteger i = 0; i < MIN(_candidates.count, 3); i++) {
        NSLog(@"  [%lu] %@", (unsigned long)i, _candidates[i].text);
    }

    // 将候选词存储到 SelectionManager
    std::vector<sime::Candidate> candidates;
    for (SimeCandidate *cand in _candidates) {
        std::string text([cand.text UTF8String]);
        candidates.emplace_back(text, cand.score, cand.matchedLength);
    }
    _selectionManager->SetCandidates(std::move(candidates));
    _selectionManager->SetCachedPinyin(currentPinyinStr);

    if (_candidates.count > 0) {
        [self showCandidatesWindow];
    } else {
        [self hideCandidatesWindow];
    }
}

- (void)commitSelectedCandidate {
    if (_selectedIndex >= 0 && _selectedIndex < (NSInteger)_candidates.count) {
        // 使用 SelectionManager 的选词逻辑
        auto result = _selectionManager->SelectCandidate(static_cast<std::size_t>(_selectedIndex));

        if (result.should_commit) {
            // 拼音全部消耗完毕，提交所有已选文字
            NSString *textToCommit = [NSString stringWithUTF8String:result.text_to_commit.c_str()];
            [self commitText:textToCommit];
        } else {
            // 还有剩余拼音，继续输入
            _currentPinyin = [NSString stringWithUTF8String:result.remaining_pinyin.c_str()];
            _selectedIndex = 0;

            // 更新 marked text 以显示已选文字 + 剩余拼音
            id client = [self client];
            if (client && [client respondsToSelector:@selector(setMarkedText:selectionRange:replacementRange:)]) {
                // 构建显示文本：已选文字（加粗下划线）+ 剩余拼音（普通）
                NSMutableAttributedString *markedText = [[NSMutableAttributedString alloc] init];

                // 第一部分：已选文字（加粗+下划线）
                if (_selectionManager->HasSelections()) {
                    std::string committedText = _selectionManager->GetCommittedText();
                    NSString *committedStr = [NSString stringWithUTF8String:committedText.c_str()];
                    NSMutableAttributedString *committedPart = [[NSMutableAttributedString alloc] initWithString:committedStr];
                    [committedPart addAttribute:NSUnderlineStyleAttributeName
                                         value:@(NSUnderlineStyleSingle)
                                         range:NSMakeRange(0, committedStr.length)];
                    [committedPart addAttribute:NSFontAttributeName
                                         value:[NSFont boldSystemFontOfSize:[NSFont systemFontSize]]
                                         range:NSMakeRange(0, committedStr.length)];
                    [markedText appendAttributedString:committedPart];
                }

                // 第二部分：剩余拼音（普通样式）
                if (_currentPinyin.length > 0) {
                    NSAttributedString *pinyinPart = [[NSAttributedString alloc] initWithString:_currentPinyin];
                    [markedText appendAttributedString:pinyinPart];
                }

                [client setMarkedText:markedText
                       selectionRange:NSMakeRange(markedText.length, 0)
                  replacementRange:NSMakeRange(NSNotFound, NSNotFound)];
            }

            // 更新候选词
            [self updateCandidates];
        }
    }
}

- (void)commitText:(NSString *)text {
    NSLog(@"[SimeInputController] commitText: %@", text);

    if (text.length == 0) {
        return;
    }

    id client = [self client];
    NSLog(@"[SimeInputController] client: %@", client);

    // Insert the final text
    [client insertText:text replacementRange:NSMakeRange(NSNotFound, NSNotFound)];

    // 重置 SelectionManager 状态
    _selectionManager->Reset();
    _currentPinyin = @"";
    _candidates = @[];
    _selectedIndex = 0;
    _currentPage = 0;

    [self hideCandidatesWindow];
}

- (void)cancelInput {
    if (_currentPinyin.length > 0 || _selectionManager->HasSelections()) {
        // Clear marked text in client
        id client = [self client];
        if (client && [client respondsToSelector:@selector(setMarkedText:selectionRange:replacementRange:)]) {
            [client setMarkedText:@""
                   selectionRange:NSMakeRange(0, 0)
              replacementRange:NSMakeRange(NSNotFound, NSNotFound)];
        }

        // 重置 SelectionManager 状态
        _selectionManager->Reset();
        _currentPinyin = @"";
        _candidates = @[];
        _selectedIndex = 0;
        _currentPage = 0;
        [self hideCandidatesWindow];
    }
}

- (BOOL)handleNumberKey:(unichar)ch {
    if (_candidates.count == 0) {
        return NO;
    }

    NSUInteger keyIndex = ch - '1';  // '1' -> 0, '2' -> 1, etc.

    // 计算当前页的候选索引
    NSInteger pageStartIndex = _currentPage * kPageSize;
    NSInteger pageEndIndex = MIN((_currentPage + 1) * kPageSize - 1, (NSInteger)_candidates.count - 1);
    NSInteger pageSize = pageEndIndex - pageStartIndex + 1;

    NSLog(@"[SimeInputController] Number key %C pressed, keyIndex: %lu, page: %ld, pageSize: %ld",
          ch, (unsigned long)keyIndex, (long)_currentPage, (long)pageSize);

    // 检查数字键是否在当前页范围内
    if (keyIndex < (NSUInteger)pageSize) {
        _selectedIndex = pageStartIndex + keyIndex;
        NSLog(@"[SimeInputController] Selecting candidate at global index: %ld", (long)_selectedIndex);
        [self commitSelectedCandidate];
        return YES;
    }

    // If number is out of range, don't pass it through - just ignore it
    NSLog(@"[SimeInputController] Number key out of range for current page, ignoring");
    return YES;  // Return YES to prevent system from handling it
}

- (BOOL)handlePageUp {
    if (_currentPage > 0) {
        [self goToPreviousPage];
    }
    return YES;
}

- (BOOL)handlePageDown {
    if ((_currentPage + 1) * kPageSize < (NSInteger)_candidates.count) {
        [self goToNextPage];
    }
    return YES;
}

- (void)goToNextPage {
    if ((_currentPage + 1) * kPageSize >= (NSInteger)_candidates.count) {
        return;
    }

    _currentPage++;
    // 将选中项设置为新页面的第一个候选
    _selectedIndex = _currentPage * kPageSize;
    NSLog(@"[SimeInputController] goToNextPage: page %ld, selectedIndex: %ld", (long)_currentPage, (long)_selectedIndex);
    [self updateCandidatesWindow];
}

- (void)goToPreviousPage {
    if (_currentPage == 0) {
        return;
    }

    _currentPage--;
    // 将选中项设置为新页面的第一个候选
    _selectedIndex = _currentPage * kPageSize;
    NSLog(@"[SimeInputController] goToPreviousPage: page %ld, selectedIndex: %ld", (long)_currentPage, (long)_selectedIndex);
    [self updateCandidatesWindow];
}

- (void)selectNextCandidate {
    if (_candidates.count == 0) return;

    NSInteger pageStartIndex = _currentPage * kPageSize;
    NSInteger pageEndIndex = MIN((_currentPage + 1) * kPageSize - 1, (NSInteger)_candidates.count - 1);

    NSInteger oldIndex = _selectedIndex;
    if (_selectedIndex < pageEndIndex) {
        _selectedIndex++;
    } else {
        // 循环到当前页的第一个
        _selectedIndex = pageStartIndex;
    }
    NSLog(@"[SimeInputController] selectNextCandidate: %ld -> %ld (page %ld)", (long)oldIndex, (long)_selectedIndex, (long)_currentPage);
    [self updateCandidatesWindow];
}

- (void)selectPreviousCandidate {
    if (_candidates.count == 0) return;

    NSInteger pageStartIndex = _currentPage * kPageSize;
    NSInteger pageEndIndex = MIN((_currentPage + 1) * kPageSize - 1, (NSInteger)_candidates.count - 1);

    NSInteger oldIndex = _selectedIndex;
    if (_selectedIndex > pageStartIndex) {
        _selectedIndex--;
    } else {
        // 循环到当前页的最后一个
        _selectedIndex = pageEndIndex;
    }
    NSLog(@"[SimeInputController] selectPreviousCandidate: %ld -> %ld (page %ld)", (long)oldIndex, (long)_selectedIndex, (long)_currentPage);
    [self updateCandidatesWindow];
}

#pragma mark - Candidates Window

- (void)showCandidatesWindow {
    NSLog(@"[SimeInputController] showCandidatesWindow called, candidates count: %lu, page: %ld, selectedIndex: %ld",
          (unsigned long)_candidates.count, (long)_currentPage, (long)_selectedIndex);

    // 如果总候选数为 0，不显示
    if (_candidates.count == 0) {
        NSLog(@"[SimeInputController] No candidates, hiding window");
        [self hideCandidatesWindow];
        return;
    }

    if (!_candidatesWindow) {
        NSLog(@"[SimeInputController] Creating new candidates window");
        _candidatesWindow = [[SimeCandidatesWindow alloc] init];
    }

    // 确保 currentPage 在有效范围内
    NSInteger totalPages = (_candidates.count + kPageSize - 1) / kPageSize;
    if (_currentPage >= totalPages) {
        NSLog(@"[SimeInputController] WARNING: currentPage (%ld) >= totalPages (%ld), resetting to 0",
              (long)_currentPage, (long)totalPages);
        _currentPage = 0;
        _selectedIndex = 0;
    }

    // 只传入当前页的候选
    NSArray<SimeCandidate *> *pageCandidates = [self getCurrentPageCandidates];
    if (pageCandidates.count == 0) {
        NSLog(@"[SimeInputController] ERROR: pageCandidates is empty, but _candidates has %lu items",
              (unsigned long)_candidates.count);
        // 尝试修复：重置到第一页
        _currentPage = 0;
        _selectedIndex = 0;
        pageCandidates = [self getCurrentPageCandidates];
        if (pageCandidates.count == 0) {
            NSLog(@"[SimeInputController] Still no pageCandidates after reset");
            return;
        }
    }

    NSInteger pageRelativeIndex = _selectedIndex - _currentPage * kPageSize;

    // 边界检查：确保 pageRelativeIndex 有效
    if (pageRelativeIndex < 0) {
        NSLog(@"[SimeInputController] WARNING: pageRelativeIndex is negative (%ld), resetting to 0", (long)pageRelativeIndex);
        pageRelativeIndex = 0;
        _selectedIndex = _currentPage * kPageSize;
    } else if (pageRelativeIndex >= (NSInteger)pageCandidates.count) {
        NSLog(@"[SimeInputController] WARNING: pageRelativeIndex (%ld) >= pageCandidates.count (%lu), resetting to 0",
              (long)pageRelativeIndex, (unsigned long)pageCandidates.count);
        pageRelativeIndex = 0;
        _selectedIndex = _currentPage * kPageSize;
    }

    NSLog(@"[SimeInputController] Showing %lu candidates from page %ld, relativeIndex: %ld",
          (unsigned long)pageCandidates.count, (long)_currentPage, (long)pageRelativeIndex);
    [_candidatesWindow updateCandidates:pageCandidates selectedIndex:pageRelativeIndex];
    NSPoint caretPos = [self caretPosition];
    NSLog(@"[SimeInputController] Showing window at position: (%.1f, %.1f)", caretPos.x, caretPos.y);
    [_candidatesWindow showAtCaretPosition:caretPos];
    NSLog(@"[SimeInputController] Window shown, isVisible: %@", _candidatesWindow.isVisible ? @"YES" : @"NO");
}

- (void)hideCandidatesWindow {
    if (_candidatesWindow) {
        [_candidatesWindow orderOut:nil];
    }
}

- (void)updateCandidatesWindow {
    NSLog(@"[SimeInputController] updateCandidatesWindow called, selectedIndex: %ld, page: %ld, candidates: %lu",
          (long)_selectedIndex, (long)_currentPage, (unsigned long)_candidates.count);

    if (!_candidatesWindow) {
        NSLog(@"[SimeInputController] WARNING: _candidatesWindow is nil!");
        return;
    }

    if (_candidates.count == 0) {
        NSLog(@"[SimeInputController] No candidates in update");
        [self hideCandidatesWindow];
        return;
    }

    // 确保 currentPage 在有效范围内
    NSInteger totalPages = (_candidates.count + kPageSize - 1) / kPageSize;
    if (_currentPage >= totalPages) {
        NSLog(@"[SimeInputController] WARNING: currentPage (%ld) >= totalPages (%ld) in update, resetting to 0",
              (long)_currentPage, (long)totalPages);
        _currentPage = 0;
        _selectedIndex = 0;
    }

    // 只传入当前页的候选
    NSArray<SimeCandidate *> *pageCandidates = [self getCurrentPageCandidates];
    if (pageCandidates.count == 0) {
        NSLog(@"[SimeInputController] ERROR: pageCandidates is empty in update, but _candidates has %lu items",
              (unsigned long)_candidates.count);
        // 尝试修复：重置到第一页
        _currentPage = 0;
        _selectedIndex = 0;
        pageCandidates = [self getCurrentPageCandidates];
        if (pageCandidates.count == 0) {
            NSLog(@"[SimeInputController] Still no pageCandidates in update after reset");
            return;
        }
    }

    NSInteger pageRelativeIndex = _selectedIndex - _currentPage * kPageSize;

    // 边界检查：确保 pageRelativeIndex 有效
    if (pageRelativeIndex < 0) {
        NSLog(@"[SimeInputController] WARNING: pageRelativeIndex is negative (%ld) in update, resetting to 0", (long)pageRelativeIndex);
        pageRelativeIndex = 0;
        _selectedIndex = _currentPage * kPageSize;
    } else if (pageRelativeIndex >= (NSInteger)pageCandidates.count) {
        NSLog(@"[SimeInputController] WARNING: pageRelativeIndex (%ld) >= pageCandidates.count (%lu) in update, resetting to 0",
              (long)pageRelativeIndex, (unsigned long)pageCandidates.count);
        pageRelativeIndex = 0;
        _selectedIndex = _currentPage * kPageSize;
    }

    [_candidatesWindow updateCandidates:pageCandidates selectedIndex:pageRelativeIndex];
}

- (NSArray<SimeCandidate *> *)getCurrentPageCandidates {
    if (_candidates.count == 0) {
        return @[];
    }

    NSInteger pageStartIndex = _currentPage * kPageSize;
    NSInteger pageEndIndex = MIN((_currentPage + 1) * kPageSize, (NSInteger)_candidates.count);

    if (pageStartIndex >= (NSInteger)_candidates.count) {
        return @[];
    }

    NSRange range = NSMakeRange(pageStartIndex, pageEndIndex - pageStartIndex);
    return [_candidates subarrayWithRange:range];
}

- (NSPoint)caretPosition {
    id client = [self client];
    NSRect caretRect = NSZeroRect;
    
    @try {
        // Try to get caret position from client
        if ([client respondsToSelector:@selector(firstRectForCharacterRange:)]) {
            caretRect = [client firstRectForCharacterRange:NSMakeRange(NSNotFound, 0)];
        }
        
        // Convert to screen coordinates
        if ([client respondsToSelector:@selector(bundleIdentifier)]) {
            NSString *bundleId = [client bundleIdentifier];
            for (NSRunningApplication *app in [NSRunningApplication runningApplicationsWithBundleIdentifier:bundleId]) {
                caretRect.origin = [NSEvent mouseLocation];
                break;
            }
        }
    }
    @catch (NSException *e) {
        // Fallback to mouse location
        caretRect.origin = [NSEvent mouseLocation];
    }
    
    // Default to mouse location if we couldn't get caret position
    if (NSEqualPoints(caretRect.origin, NSZeroPoint)) {
        caretRect.origin = [NSEvent mouseLocation];
    }
    
    return caretRect.origin;
}

#pragma mark - Menu Actions

- (NSMenu *)menu {
    NSMenu *menu = [[NSMenu alloc] init];
    
    NSMenuItem *aboutItem = [[NSMenuItem alloc] initWithTitle:@"About SimeIME"
                                                        action:@selector(showAbout:)
                                                 keyEquivalent:@""];
    [aboutItem setTarget:self];
    [menu addItem:aboutItem];
    
    [menu addItem:[NSMenuItem separatorItem]];
    
    NSMenuItem *prefsItem = [[NSMenuItem alloc] initWithTitle:@"Preferences..."
                                                       action:@selector(showPreferences:)
                                                keyEquivalent:@","];
    [prefsItem setTarget:self];
    [menu addItem:prefsItem];
    
    return menu;
}

- (void)showAbout:(id)sender {
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = @"SimeIME";
    alert.informativeText = @"Sime Chinese Input Method for macOS\n\nBased on the Sime input method engine.\nUses Sunpinyin dictionary and language model.";
    [alert runModal];
}

- (void)showPreferences:(id)sender {
    // TODO: Implement preferences window
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = @"Preferences";
    alert.informativeText = @"Preferences panel is not yet implemented.";
    [alert runModal];
}

@end
