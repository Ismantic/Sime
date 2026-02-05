//
//  SimeEngine.mm
//  SimeIME
//
//  C++ Engine wrapper implementation
//

#import "SimeEngine.h"

#include "interpret.h"
#include "ustr.h"
#include <filesystem>

// MARK: - SimeCandidate Implementation

@implementation SimeCandidate {
    NSString *_text;
    double _score;
    NSUInteger _index;
    NSUInteger _matchedLength;
}

- (instancetype)initWithText:(NSString *)text
                       score:(double)score
                       index:(NSUInteger)index
               matchedLength:(NSUInteger)matchedLength {
    self = [super init];
    if (self) {
        _text = [text copy];
        _score = score;
        _index = index;
        _matchedLength = matchedLength;
    }
    return self;
}

- (NSString *)text { return _text; }
- (double)score { return _score; }
- (NSUInteger)index { return _index; }
- (NSUInteger)matchedLength { return _matchedLength; }

- (NSString *)description {
    return [NSString stringWithFormat:@"<%@: %@ (score: %.3f, matched: %lu)",
            NSStringFromClass([self class]), _text, _score, (unsigned long)_matchedLength];
}

@end

// MARK: - SimeEngine Implementation

@interface SimeEngine () {
    std::unique_ptr<sime::Interpreter> _interpreter;
}
@end

@implementation SimeEngine

+ (instancetype)sharedEngine {
    static SimeEngine *sharedInstance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        sharedInstance = [[self alloc] init];
    });
    return sharedInstance;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _interpreter = std::make_unique<sime::Interpreter>();
    }
    return self;
}

- (void)dealloc {
    _interpreter.reset();
}

- (BOOL)loadResources {
    if (_interpreter->Ready()) {
        return YES;
    }
    
    // Find resources in the bundle
    NSBundle *bundle = [NSBundle bundleForClass:[self class]];
    NSURL *dictURL = [bundle URLForResource:@"pydict_sc.ime" withExtension:@"bin"];
    NSURL *lmURL = [bundle URLForResource:@"lm_sc" withExtension:@"t3g"];
    
    // Also check in the parent bundle (for input method bundle structure)
    if (!dictURL || !lmURL) {
        bundle = [NSBundle mainBundle];
        dictURL = [bundle URLForResource:@"pydict_sc.ime" withExtension:@"bin"];
        lmURL = [bundle URLForResource:@"lm_sc" withExtension:@"t3g"];
    }
    
    // Check common locations
    if (!dictURL) {
        NSArray *searchPaths = @[
            @"/usr/share/sunpinyin/pydict_sc.ime.bin",
            @"/usr/local/share/sunpinyin/pydict_sc.ime.bin",
            [NSHomeDirectory() stringByAppendingPathComponent:@"pydict_sc.ime.bin"],
        ];
        for (NSString *path in searchPaths) {
            if ([[NSFileManager defaultManager] fileExistsAtPath:path]) {
                dictURL = [NSURL fileURLWithPath:path];
                break;
            }
        }
    }
    
    if (!lmURL) {
        NSArray *searchPaths = @[
            @"/usr/share/sunpinyin/lm_sc.t3g",
            @"/usr/local/share/sunpinyin/lm_sc.t3g",
            [NSHomeDirectory() stringByAppendingPathComponent:@"lm_sc.t3g"],
        ];
        for (NSString *path in searchPaths) {
            if ([[NSFileManager defaultManager] fileExistsAtPath:path]) {
                lmURL = [NSURL fileURLWithPath:path];
                break;
            }
        }
    }
    
    if (!dictURL || !lmURL) {
        NSLog(@"[SimeEngine] Failed to find resources. Dict: %@, LM: %@", dictURL, lmURL);
        return NO;
    }
    
    NSLog(@"[SimeEngine] Loading dictionary: %@", dictURL.path);
    NSLog(@"[SimeEngine] Loading language model: %@", lmURL.path);
    
    std::filesystem::path dictPath([dictURL.path UTF8String]);
    std::filesystem::path lmPath([lmURL.path UTF8String]);
    
    if (!_interpreter->LoadResources(dictPath, lmPath)) {
        NSLog(@"[SimeEngine] Failed to load resources");
        return NO;
    }
    
    NSLog(@"[SimeEngine] Resources loaded successfully");
    return YES;
}

- (BOOL)isReady {
    return _interpreter->Ready() ? YES : NO;
}

- (NSArray<SimeCandidate *> *)decodePinyin:(NSString *)pinyin
                                       nbest:(NSUInteger)nbest {
    if (!_interpreter->Ready()) {
        return @[];
    }

    std::string pinyinStr([pinyin UTF8String]);
    sime::DecodeOptions opts;
    opts.num = nbest > 0 ? nbest : 5;

    auto results = _interpreter->DecodeText(pinyinStr, opts);

    NSMutableArray<SimeCandidate *> *candidates = [NSMutableArray array];
    for (size_t i = 0; i < results.size(); i++) {
        std::string utf8 = sime::ustr::FromU32(results[i].text);
        NSString *text = [[NSString alloc] initWithUTF8String:utf8.c_str()];
        if (text) {
            SimeCandidate *candidate = [[SimeCandidate alloc] initWithText:text
                                                                      score:results[i].score
                                                                      index:i
                                                              matchedLength:results[i].matched_length];
            [candidates addObject:candidate];
        }
    }

    return [candidates copy];
}

+ (BOOL)isValidPinyinChar:(unichar)ch {
    // Valid pinyin characters: a-z, A-Z, and tone digits 1-5
    return (ch >= 'a' && ch <= 'z') ||
           (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9');
}

+ (BOOL)isValidPinyinString:(NSString *)str {
    if (str.length == 0) return NO;
    
    for (NSUInteger i = 0; i < str.length; i++) {
        unichar ch = [str characterAtIndex:i];
        if (![self isValidPinyinChar:ch]) {
            return NO;
        }
    }
    return YES;
}

@end
