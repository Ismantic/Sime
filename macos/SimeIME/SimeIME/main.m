//
//  main.m
//  SimeIME
//
//  Entry point for Sime Input Method
//

#import <Foundation/Foundation.h>
#import <Cocoa/Cocoa.h>
#import <InputMethodKit/InputMethodKit.h>
#import "SimeInputController.h"

// The connection name must be unique and match Info.plist
static NSString *const kConnectionName = @"com.ismantic.SimeIME_1_Connection";

// Keep the server as a global variable so it doesn't get deallocated
static IMKServer *gServer = nil;

int main(int argc, char *argv[]) {
    @autoreleasepool {
        // Get the bundle identifier
        NSString *bundleIdentifier = [[NSBundle mainBundle] bundleIdentifier];
        NSLog(@"[SimeIME] Starting input method: %@", bundleIdentifier);
        NSLog(@"[SimeIME] Connection name: %@", kConnectionName);

        // Create the IMKServer
        // Note: bundleIdentifier parameter is actually ignored by the system
        // The system reads configuration from Info.plist instead
        gServer = [[IMKServer alloc] initWithName:kConnectionName
                                 bundleIdentifier:bundleIdentifier];

        if (!gServer) {
            NSLog(@"[SimeIME] Failed to create IMKServer");
            return 1;
        }

        NSLog(@"[SimeIME] IMKServer created successfully");
        NSLog(@"[SimeIME] Connection name: %@", kConnectionName);
        NSLog(@"[SimeIME] Bundle identifier: %@", bundleIdentifier);
        NSLog(@"[SimeIME] Waiting for client connections...");

        // Run the application
        [[NSApplication sharedApplication] run];
    }

    return 0;
}
