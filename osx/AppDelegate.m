//
//  AppDelegate.m
//  Vantage
//
//  Created by Joe Drago on 9/5/19.
//  Copyright © 2019 Joe Drago. All rights reserved.
//

#import "AppDelegate.h"
#import "GameViewController.h"

@interface AppDelegate ()

@end

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    // Insert code here to initialize your application
}

- (void)applicationWillTerminate:(NSNotification *)aNotification {
    // Insert code here to tear down your application
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    return YES;
}

- (IBAction)openDocument:sender
{
    [[NSNotificationCenter defaultCenter] postNotificationName:@"openDocument" object:self];
}

@end
