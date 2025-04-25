//
//  msputils.m
//  MSPUtils
//
//  Created by Tom Kidd on 6/29/22.
//

#import <Foundation/Foundation.h>
#import <Cocoa/Cocoa.h>
//#import "msputils.h"

const char* getAppSupportDirectory(const char* subdir) {
    NSArray *paths = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
    NSString *applicationSupportDirectory = [[paths firstObject] stringByAppendingPathComponent:[NSString stringWithUTF8String:subdir]];
    return [applicationSupportDirectory cStringUsingEncoding:NSUTF8StringEncoding];
}

#if 0
const char* getBundlePath() {
    NSString *bundlePath = [[[NSBundle mainBundle] bundleURL] path];
    return [bundlePath cStringUsingEncoding:NSASCIIStringEncoding];
}

const char* getBundlePathSubdir(const char* subdir) {
    NSString *bundlePath = [[[[NSBundle mainBundle] bundleURL] path] stringByAppendingPathComponent:[NSString stringWithUTF8String:subdir]];
    return [bundlePath cStringUsingEncoding:NSASCIIStringEncoding];
}

const char* getBundlePathSubdirAndFile(const char* subdir, const char* file) {
    NSString *bundlePath = [[[[[NSBundle mainBundle] bundleURL] path] stringByAppendingPathComponent:[NSString stringWithUTF8String:subdir]]stringByAppendingPathComponent:[NSString stringWithUTF8String:file]];
    return [bundlePath cStringUsingEncoding:NSASCIIStringEncoding];
}

void displayMessageBox(const char* title, const char* message, int isError) {
    NSAlert *alert = [NSAlert new];
    NSString *titleStr = [NSString stringWithCString:title encoding:NSASCIIStringEncoding];
    NSString *msgStr = [NSString stringWithCString:message encoding:NSASCIIStringEncoding];

    if (alert && titleStr && msgStr) {
        alert.alertStyle = isError ? NSAlertStyleCritical : NSAlertStyleInformational;
        alert.messageText = titleStr;
        alert.informativeText = msgStr;

//        [alert performSelectorOnMainThread:@selector(runModal) withObject:nil waitUntilDone:NO];
        [alert runModal];
    }

    [alert release];
    [titleStr release];
    [msgStr release];
}
#endif
