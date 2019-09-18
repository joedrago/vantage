// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#import "VantageView.h"

@interface VantageView ()

@end

@implementation VantageView

- (void)allowDragAndDrop
{
    self.V = vantageCreate(); // where to clean this up?

    [self registerForDraggedTypes:[NSArray arrayWithObjects:NSPasteboardTypeFileURL, nil]];
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender
{
    NSPasteboard * pboard;
    NSDragOperation sourceDragMask;

    sourceDragMask = [sender draggingSourceOperationMask];
    pboard = [sender draggingPasteboard];

    if ([[pboard types] containsObject:NSPasteboardTypeFileURL]) {
        return NSDragOperationGeneric;
    }
    return NSDragOperationNone;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender
{
    NSPasteboard * pboard = [sender draggingPasteboard];
    if ([[pboard types] containsObject:NSPasteboardTypeFileURL]) {
        NSArray * urls = [pboard readObjectsForClasses:[NSArray arrayWithObject:[NSURL class]]
                                               options:[NSDictionary dictionaryWithObject:[NSNumber numberWithBool:YES]
                                                                                   forKey:NSPasteboardURLReadingFileURLsOnlyKey]];
        int urlCount = [urls count];
        if (urlCount == 2) {
            NSURL * url1 = [urls objectAtIndex:0];
            NSURL * url2 = [urls objectAtIndex:1];
            [self.renderer loadImage:url1.path diff:url2.path];
        } else if (urlCount > 0) {
            NSURL * url = [urls objectAtIndex:0];
            [self.renderer loadImage:url.path diff:nil];
        }
    }
    return YES;
}

- (void)mouseDown:(NSEvent *)event
{
    [self.renderer mouseDown:event];
}

- (void)mouseUp:(NSEvent *)event
{
    [self.renderer mouseUp:event];
}

- (void)mouseDragged:(NSEvent *)event
{
    [self.renderer mouseDragged:event];
}

- (void)rightMouseDown:(NSEvent *)event
{
    [self.renderer rightMouseDown:event];
}

- (void)rightMouseDragged:(NSEvent *)event
{
    [self.renderer rightMouseDragged:event];
}

- (void)scrollWheel:(NSEvent *)event
{
    [self.renderer scrollWheel:event];
}

// - (BOOL)acceptsFirstResponder
// {
//     return YES;
// }

// - (BOOL)becomeFirstResponder
// {
//     return YES;
// }

// - (BOOL)resignFirstResponder
// {
//     return YES;
// }

// - (void)keyUp:(NSEvent *)event
// {
//     if (![self.renderer keyUp:event]) {
//         [super keyUp:event];
//     }
// }

@end
