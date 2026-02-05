//
//  SimeEngine.h
//  SimeIME
//
//  C++ Engine wrapper for macOS Input Method
//

#import <Foundation/Foundation.h>
#import <string>
#import <vector>

NS_ASSUME_NONNULL_BEGIN

@interface SimeCandidate : NSObject
@property(nonatomic, readonly) NSString *text;
@property(nonatomic, readonly) double score;
@property(nonatomic, readonly) NSUInteger index;
@property(nonatomic, readonly) NSUInteger matchedLength;

- (instancetype)initWithText:(NSString *)text
                       score:(double)score
                       index:(NSUInteger)index
               matchedLength:(NSUInteger)matchedLength;
@end

@interface SimeEngine : NSObject

+ (instancetype)sharedEngine;

/// Load dictionary and language model from resources
- (BOOL)loadResources;

/// Check if engine is ready
- (BOOL)isReady;

/// Decode pinyin input to candidates
/// @param pinyin The pinyin string (e.g., "zhongwen")
/// @param nbest Number of candidates to return
/// @return Array of SimeCandidate objects
- (NSArray<SimeCandidate *> *)decodePinyin:(NSString *)pinyin
                                       nbest:(NSUInteger)nbest;

/// Check if a character is a valid pinyin character
+ (BOOL)isValidPinyinChar:(unichar)ch;

/// Check if string is valid pinyin input
+ (BOOL)isValidPinyinString:(NSString *)str;

@end

NS_ASSUME_NONNULL_END
