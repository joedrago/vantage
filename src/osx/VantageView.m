// ---------------------------------------------------------------------------
//                         Copyright Joe Drago 2019.
//         Distributed under the Boost Software License, Version 1.0.
//            (See accompanying file LICENSE_1_0.txt or copy at
//                  http://www.boost.org/LICENSE_1_0.txt)
// ---------------------------------------------------------------------------

#import "VantageView.h"

@interface VantageView ()

@end

@implementation VantageView

- (void)allowDragAndDrop
{
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
        if ([urls count] == 1) {
            NSURL * url = [urls objectAtIndex:0];
            [self.renderer loadImage:url.path diff:nil];
        }
    }
    return YES;
}

// - (void)mouseDragged:(NSEvent *)event
// {
//     [self.renderer mouseDragged:event];
// }

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
