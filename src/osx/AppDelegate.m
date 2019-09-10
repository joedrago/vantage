// ---------------------------------------------------------------------------
//                         Copyright Joe Drago 2019.
//         Distributed under the Boost Software License, Version 1.0.
//            (See accompanying file LICENSE_1_0.txt or copy at
//                  http://www.boost.org/LICENSE_1_0.txt)
// ---------------------------------------------------------------------------

#import "AppDelegate.h"

#import "GameViewController.h"

@interface AppDelegate ()

@end

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
}

- (void)applicationWillTerminate:(NSNotification *)aNotification
{
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender
{
    return YES;
}

- (IBAction)openDocument:sender
{
    [[NSNotificationCenter defaultCenter] postNotificationName:@"openDocument" object:self];
}

- (IBAction)toggleHDR:sender
{
    [[NSNotificationCenter defaultCenter] postNotificationName:@"toggleHDR" object:self];
}

- (IBAction)toggleSRGBHighlight:sender
{
    [[NSNotificationCenter defaultCenter] postNotificationName:@"toggleSRGBHighlight" object:self];
}

- (BOOL)application:(NSApplication *)sender openFile:(NSString *)filename
{
    NSDictionary * userInfo = @{ @"filename" : filename };
    [[NSNotificationCenter defaultCenter] postNotificationName:@"openFile" object:self userInfo:userInfo];
    return YES;
}

@end
