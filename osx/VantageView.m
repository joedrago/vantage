//
//  VantageView.m
//  Vantage
//
//  Created by Joe Drago on 9/5/19.
//  Copyright © 2019 Joe Drago. All rights reserved.
//

#import "VantageView.h"

@interface VantageView ()

@end

@implementation VantageView

- (void)allowDragAndDrop
{
    [self registerForDraggedTypes:[NSArray arrayWithObjects: NSPasteboardTypeFileURL, nil]];
}

- (NSDragOperation)draggingEntered:(id <NSDraggingInfo>)sender {
    NSPasteboard *pboard;
    NSDragOperation sourceDragMask;

    sourceDragMask = [sender draggingSourceOperationMask];
    pboard = [sender draggingPasteboard];

    if ( [[pboard types] containsObject:NSPasteboardTypeFileURL] ) {
        return NSDragOperationGeneric;
    }
    return NSDragOperationNone;
}

- (BOOL)performDragOperation:(id <NSDraggingInfo>)sender {
    NSPasteboard *pboard;
    NSDragOperation sourceDragMask;

    pboard = [sender draggingPasteboard];

    if ( [[pboard types] containsObject:NSPasteboardTypeFileURL] ) {
        NSArray *urls = [pboard propertyListForType:NSPasteboardTypeFileURL];
        if([urls count] > 0) {
            [self.renderer loadImage:[urls objectAtIndex:0]];
        }
    }
    return YES;
}

@end
