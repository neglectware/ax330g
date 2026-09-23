// winshot <owner name substring> <out.png>: screenshot one app's front window.
#import <Cocoa/Cocoa.h>
int main(int argc, char **argv) {
    @autoreleasepool {
        NSArray *wins = (__bridge_transfer NSArray *)CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
        for (NSDictionary *w in wins) {
            if (![w[(id)kCGWindowOwnerName] containsString:@(argv[1])]) continue;
            if ([w[(id)kCGWindowLayer] intValue] != 0) continue;
            CGWindowID wid = [w[(id)kCGWindowNumber] unsignedIntValue];
            NSTask *t = [NSTask new];
            t.executableURL = [NSURL fileURLWithPath:@"/usr/sbin/screencapture"];
            t.arguments = @[@"-x", @"-o", [NSString stringWithFormat:@"-l%u", wid], @(argv[2])];
            [t launchAndReturnError:nil]; [t waitUntilExit];
            printf("window %u -> %s (exit %d)\n", wid, argv[2], t.terminationStatus);
            return t.terminationStatus;
        }
        fprintf(stderr, "window not found\n");
        return 1;
    }
}
