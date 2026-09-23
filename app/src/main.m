#import <Cocoa/Cocoa.h>
#import "AXAppDelegate.h"
#import "AXAudio.h"
// --selftest "<device substring>" <out.wav> [L R ref] : record 3 s from the
// inputs while playing silence on outputs 1-2. Verifies device selection,
// engine start and the 3-channel file without making a sound.
static int selftest(int argc, const char *argv[]) {
    NSString *want = @(argv[2]);
    AXDevice *dev = nil;
    for (AXDevice *d in [AXAudio devices]) { printf("device: %s (%ld in / %ld out)\n", d.name.UTF8String, (long)d.inputs, (long)d.outputs); if ([d.name containsString:want]) dev = d; }
    if (!dev) { fprintf(stderr, "no device matching %s\n", argv[2]); return 1; }
    AXAudio *a = [AXAudio new];
    a.inputChannels = argc >= 8 ? @[@(atoi(argv[5])), @(atoi(argv[6])), @(atoi(argv[7]))] : @[@2, @3, @4];
    a.outputPairBase = 0;
    float *hold = calloc(3, sizeof(float));
    a.levelBlock = ^(float *p, NSInteger n) { for (NSInteger i = 0; i < n && i < 3; i++) if (p[i] > hold[i]) hold[i] = p[i]; };
    NSError *e = nil;
    if (![a startWithDevice:dev sampleRate:0 error:&e]) { fprintf(stderr, "start: %s\n", e.localizedDescription.UTF8String); return 2; }
    printf("engine running on %s at %.0f Hz\n", dev.name.UTF8String, a.sampleRate);
    AVAudioFormat *mono = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:a.sampleRate channels:1];
    AVAudioPCMBuffer *silence = [[AVAudioPCMBuffer alloc] initWithPCMFormat:mono frameCapacity:(AVAudioFrameCount)(a.sampleRate * 2)];
    silence.frameLength = silence.frameCapacity;
    memset(silence.floatChannelData[0], 0, silence.frameLength * sizeof(float));
    __block BOOL done = NO;
    if (![a playrecSignal:silence tailSeconds:0.5 toURL:[NSURL fileURLWithPath:@(argv[3])] done:^(NSError *err) {
        if (err) fprintf(stderr, "playrec: %s\n", err.localizedDescription.UTF8String);
        done = YES; } error:&e]) { fprintf(stderr, "playrec start: %s\n", e.localizedDescription.UTF8String); return 3; }
    while (!done) [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
    [a stop];
    printf("peaks L %.1f R %.1f ref %.1f dBFS -> %s\n", 20 * log10f(fmaxf(hold[0], 1e-6)), 20 * log10f(fmaxf(hold[1], 1e-6)), 20 * log10f(fmaxf(hold[2], 1e-6)), argv[3]);
    return 0;
}

int main(int argc, const char *argv[]) {
    if (argc >= 4 && !strcmp(argv[1], "--selftest")) { @autoreleasepool { return selftest(argc, argv); } }
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        AXAppDelegate *d = [[AXAppDelegate alloc] init];
        app.delegate = d;
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        [app run];
    }
    return 0;
}
