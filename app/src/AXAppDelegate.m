// AX30G Bench: pick the interface, see the meters, walk a parameter grid one
// step at a time, capture (play + record on one clock, reference channel
// included), name and log the file, run the analysis and show its picture.
// The Python in ../ is the engine; this is the hands.
#import "AXAppDelegate.h"
#import "AXAudio.h"
#import "version.h"

@interface AXMeter : NSView
@property float peak, hold;
@property (copy) NSString *label;
@end
@implementation AXMeter
- (void)drawRect:(NSRect)r {
    [[NSColor colorWithWhite:0.12 alpha:1] setFill]; NSRectFill(self.bounds);
    float db = 20 * log10f(fmaxf(self.peak, 1e-6));
    float frac = fmaxf(0, (db + 60) / 60);
    NSRect bar = self.bounds; bar.size.height = 0; bar.size.width = self.bounds.size.width * frac;
    bar.size.height = self.bounds.size.height;
    [(db > -1 ? [NSColor systemRedColor] : db > -12 ? [NSColor systemYellowColor] : [NSColor systemGreenColor]) setFill];
    NSRectFill(bar);
    float hdb = 20 * log10f(fmaxf(self.hold, 1e-6));
    NSRect h = self.bounds; h.origin.x = self.bounds.size.width * fmaxf(0, (hdb + 60) / 60) - 1; h.size.width = 2;
    [[NSColor whiteColor] setFill]; NSRectFill(h);
    NSString *t = [NSString stringWithFormat:@"%@  %5.1f dBFS", self.label ?: @"", hdb];
    [t drawAtPoint:NSMakePoint(4, 2) withAttributes:@{NSFontAttributeName: [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular],
                                                       NSForegroundColorAttributeName: [NSColor whiteColor]}];
}
@end

@interface AXAppDelegate ()
@property NSWindow *win;
@property AXAudio *audio;
@property NSArray<AXDevice *> *devices;
@property NSPopUpButton *devPop, *outPop, *inL, *inR, *inRef;
@property NSPopUpButton *toneLevel, *toneHz;
@property NSString *signalVariant;   // layout.json "variant" (e.g. lfo); tags file names so alternate sets never overwrite normal-set captures
@property NSButton *monitorBtn, *toneBtn, *captureBtn, *skipBtn, *openGridBtn, *outDirBtn, *rootBtn;
@property NSTextField *status, *stepText, *outDirLabel, *rootLabel, *gridLabel;
@property AXMeter *mL, *mR, *mRef;
@property NSTableView *table;
@property NSImageView *picture;
@property NSTextView *log;
@property NSMutableArray<NSDictionary *> *grid;      // steps
@property NSMutableArray<NSString *> *doneFiles;     // parallel to grid, nil = not done
@property NSString *gridPath, *outDir, *root;
@property AVAudioPCMBuffer *signal;
@property NSInteger current;
@property NSTimer *holdTimer;
@end

@implementation AXAppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)n {
    self.audio = [AXAudio new];
    NSUserDefaults *d = [NSUserDefaults standardUserDefaults];
    self.root = [d stringForKey:@"root"] ?: [self defaultRoot];
    self.outDir = [d stringForKey:@"outDir"] ?: [self.root stringByAppendingPathComponent:@"captures"];
    self.gridPath = [d stringForKey:@"grid"];
    [self buildMenu];
    [self buildWindow];
    [self refreshDevices];
    [self loadSignal];
    if (self.gridPath) [self loadGrid:self.gridPath];
    __weak AXAppDelegate *w = self;
    self.audio.levelBlock = ^(float *p, NSInteger n) {
        AXAppDelegate *s = w; if (!s) return;
        AXMeter *ms[3] = {s.mL, s.mR, s.mRef};
        for (NSInteger i = 0; i < n && i < 3; i++) { ms[i].peak = p[i]; if (p[i] > ms[i].hold) ms[i].hold = p[i]; [ms[i] setNeedsDisplay:YES]; }
    };
    self.holdTimer = [NSTimer scheduledTimerWithTimeInterval:1.5 repeats:YES block:^(NSTimer *t) {
        AXAppDelegate *s = w; if (!s) return;
        for (AXMeter *m in @[s.mL, s.mR, s.mRef]) { m.hold *= 0.5; [m setNeedsDisplay:YES]; }
    }];
    [self logLine:[NSString stringWithFormat:@"AX30G Bench build %d. Project: %@", AX_BUILD, self.root]];
}

- (NSString *)defaultRoot {
    // the .app lives in <root>/app/, or the bare binary in <root>/app/
    NSString *p = [[NSBundle mainBundle] bundlePath];
    if ([p hasSuffix:@".app"]) p = [p stringByDeletingLastPathComponent];
    else p = [[p stringByDeletingLastPathComponent] stringByDeletingLastPathComponent];
    return [p stringByDeletingLastPathComponent];
}

- (void)buildMenu {
    NSMenu *bar = [NSMenu new];
    NSMenuItem *appItem = [NSMenuItem new]; [bar addItem:appItem];
    NSMenu *app = [NSMenu new];
    [app addItemWithTitle:@"Quit AX30G Bench" action:@selector(terminate:) keyEquivalent:@"q"];
    appItem.submenu = app;
    NSMenuItem *editItem = [NSMenuItem new]; [bar addItem:editItem];
    NSMenu *edit = [[NSMenu alloc] initWithTitle:@"Edit"];
    [edit addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"];
    [edit addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"];
    [edit addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];
    editItem.submenu = edit;
    [NSApp setMainMenu:bar];
}

static NSTextField *label(NSString *s) {
    NSTextField *t = [NSTextField labelWithString:s]; t.font = [NSFont systemFontOfSize:12]; return t;
}

- (void)buildWindow {
    NSRect fr = NSMakeRect(0, 0, 1180, 760);
    self.win = [[NSWindow alloc] initWithContentRect:fr styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable backing:NSBackingStoreBuffered defer:NO];
    self.win.title = @"AX30G Bench";
    [self.win setFrameAutosaveName:@"AXBenchWindow"];
    NSView *v = self.win.contentView;
    CGFloat W = fr.size.width, H = fr.size.height;

    // --- interface row
    CGFloat y = H - 36;
    [v addSubview:[self place:label(@"Interface") at:NSMakeRect(16, y, 70, 20)]];
    self.devPop = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(86, y - 3, 260, 26) pullsDown:NO];
    self.devPop.target = self; self.devPop.action = @selector(deviceChanged:);
    [v addSubview:self.devPop];
    [v addSubview:[self place:label(@"Out pair") at:NSMakeRect(356, y, 60, 20)]];
    self.outPop = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(416, y - 3, 90, 26) pullsDown:NO];
    [v addSubview:self.outPop];
    [v addSubview:[self place:label(@"In L / R / ref") at:NSMakeRect(516, y, 90, 20)]];
    self.inL = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(606, y - 3, 60, 26) pullsDown:NO];
    self.inR = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(668, y - 3, 60, 26) pullsDown:NO];
    self.inRef = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(730, y - 3, 60, 26) pullsDown:NO];
    for (NSPopUpButton *p in @[self.inL, self.inR, self.inRef]) [v addSubview:p];
    self.monitorBtn = [NSButton buttonWithTitle:@"Monitor" target:self action:@selector(toggleMonitor:)];
    self.monitorBtn.frame = NSMakeRect(800, y - 4, 90, 28); [self.monitorBtn setButtonType:NSButtonTypePushOnPushOff];
    [v addSubview:self.monitorBtn];
    self.toneBtn = [NSButton buttonWithTitle:@"Tone" target:self action:@selector(toggleTone:)];
    self.toneBtn.frame = NSMakeRect(895, y - 4, 60, 28); [self.toneBtn setButtonType:NSButtonTypePushOnPushOff];
    [v addSubview:self.toneBtn];
    self.toneHz = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(957, y - 3, 78, 26) pullsDown:NO];
    [self.toneHz addItemsWithTitles:@[@"100 Hz", @"1 kHz", @"4 kHz", @"10 kHz", @"16 kHz"]];
    [self.toneHz selectItemAtIndex:1];
    self.toneHz.target = self; self.toneHz.action = @selector(toneLevelChanged:);
    [v addSubview:self.toneHz];
    self.toneLevel = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(1037, y - 3, 74, 26) pullsDown:NO];
    [self.toneLevel addItemsWithTitles:@[@"−20 dB", @"−12 dB", @"−6 dB", @"−3 dB", @"0 dB"]];
    self.toneLevel.target = self; self.toneLevel.action = @selector(toneLevelChanged:);
    [v addSubview:self.toneLevel];
    self.status = label(@""); self.status.frame = NSMakeRect(1118, y, W - 1134, 20); self.status.alignment = NSTextAlignmentRight;
    [v addSubview:self.status];

    // --- meters
    y -= 30;
    self.mL = [[AXMeter alloc] initWithFrame:NSMakeRect(16, y, 370, 18)]; self.mL.label = @"AX30G L ";
    self.mR = [[AXMeter alloc] initWithFrame:NSMakeRect(400, y, 370, 18)]; self.mR.label = @"AX30G R ";
    self.mRef = [[AXMeter alloc] initWithFrame:NSMakeRect(784, y, 380, 18)]; self.mRef.label = @"reference";
    for (AXMeter *m in @[self.mL, self.mR, self.mRef]) [v addSubview:m];

    // --- grid row
    y -= 34;
    self.openGridBtn = [NSButton buttonWithTitle:@"Grid…" target:self action:@selector(openGrid:)];
    self.openGridBtn.frame = NSMakeRect(16, y - 4, 70, 28); [v addSubview:self.openGridBtn];
    self.gridLabel = label(@"(no grid)"); self.gridLabel.frame = NSMakeRect(92, y, 380, 20); [v addSubview:self.gridLabel];
    self.outDirBtn = [NSButton buttonWithTitle:@"Captures…" target:self action:@selector(chooseOutDir:)];
    self.outDirBtn.frame = NSMakeRect(480, y - 4, 100, 28); [v addSubview:self.outDirBtn];
    self.outDirLabel = label(self.outDir); self.outDirLabel.frame = NSMakeRect(586, y, 380, 20); [v addSubview:self.outDirLabel];
    self.rootBtn = [NSButton buttonWithTitle:@"Project…" target:self action:@selector(chooseRoot:)];
    self.rootBtn.frame = NSMakeRect(980, y - 4, 90, 28); [v addSubview:self.rootBtn];

    // --- table (left), step + picture (right)
    CGFloat top = y - 12;
    NSScrollView *sv = [[NSScrollView alloc] initWithFrame:NSMakeRect(16, 200, 420, top - 200)];
    self.table = [[NSTableView alloc] initWithFrame:sv.bounds];
    for (NSArray *c in @[@[@"n", @"#", @30], @[@"done", @"✓", @24], @[@"effect", @"Effect", @54], @[@"in", @"In", @26], @[@"params", @"Parameters", @280]]) {
        NSTableColumn *col = [[NSTableColumn alloc] initWithIdentifier:c[0]];
        col.title = c[1]; col.width = [c[2] doubleValue]; [self.table addTableColumn:col];
    }
    self.table.dataSource = self; self.table.delegate = self;
    self.table.rowHeight = 18; self.table.usesAlternatingRowBackgroundColors = YES;
    sv.documentView = self.table; sv.hasVerticalScroller = YES; sv.autoresizingMask = NSViewHeightSizable;
    [v addSubview:sv];

    self.stepText = [NSTextField wrappingLabelWithString:@"Open a grid, start Monitor, then Capture."];
    self.stepText.frame = NSMakeRect(450, top - 170, W - 466, 170);
    self.stepText.font = [NSFont monospacedSystemFontOfSize:14 weight:NSFontWeightRegular];
    self.stepText.autoresizingMask = NSViewWidthSizable;
    [v addSubview:self.stepText];
    self.captureBtn = [NSButton buttonWithTitle:@"Capture" target:self action:@selector(capture:)];
    self.captureBtn.frame = NSMakeRect(450, top - 210, 120, 32); self.captureBtn.keyEquivalent = @"\r";
    [v addSubview:self.captureBtn];
    self.skipBtn = [NSButton buttonWithTitle:@"Skip" target:self action:@selector(skip:)];
    self.skipBtn.frame = NSMakeRect(580, top - 210, 80, 32); [v addSubview:self.skipBtn];
    NSButton *abort = [NSButton buttonWithTitle:@"Abort" target:self action:@selector(abort:)];
    abort.frame = NSMakeRect(670, top - 210, 80, 32); [v addSubview:abort];

    NSScrollView *psv = [[NSScrollView alloc] initWithFrame:NSMakeRect(450, 200, W - 466, top - 420)];
    self.picture = [[NSImageView alloc] initWithFrame:psv.bounds];
    self.picture.imageScaling = NSImageScaleProportionallyUpOrDown; self.picture.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    psv.documentView = self.picture; psv.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [v addSubview:psv];

    NSScrollView *lsv = [[NSScrollView alloc] initWithFrame:NSMakeRect(16, 16, W - 32, 176)];
    self.log = [[NSTextView alloc] initWithFrame:lsv.bounds];
    self.log.editable = NO; self.log.font = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];
    self.log.autoresizingMask = NSViewWidthSizable;
    lsv.documentView = self.log; lsv.hasVerticalScroller = YES; lsv.autoresizingMask = NSViewWidthSizable;
    [v addSubview:lsv];
    [self.win makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (NSView *)place:(NSView *)vw at:(NSRect)r { vw.frame = r; return vw; }

- (void)logLine:(NSString *)s {
    NSString *line = [NSString stringWithFormat:@"%@  %@\n", [NSDateFormatter localizedStringFromDate:[NSDate date] dateStyle:NSDateFormatterNoStyle timeStyle:NSDateFormatterMediumStyle], s];
    [self.log.textStorage appendAttributedString:[[NSAttributedString alloc] initWithString:line attributes:@{NSFontAttributeName: self.log.font, NSForegroundColorAttributeName: [NSColor textColor]}]];
    [self.log scrollRangeToVisible:NSMakeRange(self.log.string.length, 0)];
}

// --- devices -------------------------------------------------------------
- (void)refreshDevices {
    self.devices = [AXAudio devices];
    [self.devPop removeAllItems];
    NSString *want = [[NSUserDefaults standardUserDefaults] stringForKey:@"device"];
    NSInteger sel = 0;
    for (NSInteger i = 0; i < (NSInteger)self.devices.count; i++) {
        AXDevice *d = self.devices[i];
        [self.devPop addItemWithTitle:[NSString stringWithFormat:@"%@ (%ld in / %ld out)", d.name, (long)d.inputs, (long)d.outputs]];
        if ([d.name isEqualToString:want]) sel = i;
        if (!want && [d.name containsString:@"Scarlett"]) sel = i;
    }
    if (self.devices.count) { [self.devPop selectItemAtIndex:sel]; [self deviceChanged:nil]; }
}

- (void)deviceChanged:(id)sender {
    AXDevice *d = self.devices[self.devPop.indexOfSelectedItem];
    NSUserDefaults *u = [NSUserDefaults standardUserDefaults];
    [u setObject:d.name forKey:@"device"];
    [self.outPop removeAllItems];
    for (NSInteger c = 0; c + 1 < d.outputs; c += 2) [self.outPop addItemWithTitle:[NSString stringWithFormat:@"%ld–%ld", (long)c + 1, (long)c + 2]];
    for (NSPopUpButton *p in @[self.inL, self.inR, self.inRef]) {
        [p removeAllItems];
        for (NSInteger c = 0; c < d.inputs; c++) [p addItemWithTitle:[NSString stringWithFormat:@"%ld", (long)c + 1]];
    }
    NSString *key = [NSString stringWithFormat:@"ch.%@", d.name];
    NSDictionary *saved = [u dictionaryForKey:key];
    [self.outPop selectItemAtIndex:MIN([saved[@"out"] integerValue], MAX(0, (NSInteger)self.outPop.numberOfItems - 1))];
    // default: rear line inputs 3, 4 for the pedal and 5 for the reference on an 18i20
    NSInteger dl = saved ? [saved[@"L"] integerValue] : (d.inputs >= 5 ? 2 : 0);
    NSInteger dr = saved ? [saved[@"R"] integerValue] : (d.inputs >= 5 ? 3 : MIN(1, d.inputs - 1));
    NSInteger dref = saved ? [saved[@"ref"] integerValue] : (d.inputs >= 5 ? 4 : MIN(2, d.inputs - 1));
    [self.inL selectItemAtIndex:MIN(dl, d.inputs - 1)];
    [self.inR selectItemAtIndex:MIN(dr, d.inputs - 1)];
    [self.inRef selectItemAtIndex:MIN(dref, d.inputs - 1)];
    if (self.audio.running) { self.monitorBtn.state = NSControlStateValueOff; [self toggleMonitor:nil]; }
}

- (void)applyChannels {
    AXDevice *d = self.devices[self.devPop.indexOfSelectedItem];
    self.audio.inputChannels = @[@(self.inL.indexOfSelectedItem), @(self.inR.indexOfSelectedItem), @(self.inRef.indexOfSelectedItem)];
    self.audio.outputPairBase = self.outPop.indexOfSelectedItem * 2;
    [[NSUserDefaults standardUserDefaults] setObject:@{@"out": @(self.outPop.indexOfSelectedItem), @"L": @(self.inL.indexOfSelectedItem),
                                                       @"R": @(self.inR.indexOfSelectedItem), @"ref": @(self.inRef.indexOfSelectedItem)}
                                              forKey:[NSString stringWithFormat:@"ch.%@", d.name]];
}

- (void)toggleMonitor:(id)sender {
    if (self.monitorBtn.state == NSControlStateValueOn) {
        if (!self.devices.count) { self.monitorBtn.state = NSControlStateValueOff; return; }
        [self applyChannels];
        NSError *e = nil;
        // 0 = keep the device's own rate; the signal set is resampled to it and the analysis resamples back
        if (![self.audio startWithDevice:self.devices[self.devPop.indexOfSelectedItem] sampleRate:0 error:&e]) {
            [self logLine:[NSString stringWithFormat:@"audio: %@", e.localizedDescription]];
            self.monitorBtn.state = NSControlStateValueOff;
            return;
        }
        self.status.stringValue = [NSString stringWithFormat:@"%.0f Hz, out %@ → pedal + ref, in %@/%@/%@", self.audio.sampleRate, self.outPop.titleOfSelectedItem, self.inL.titleOfSelectedItem, self.inR.titleOfSelectedItem, self.inRef.titleOfSelectedItem];
        [self logLine:[NSString stringWithFormat:@"monitoring %@", self.status.stringValue]];
    } else {
        [self.audio stop];
        self.toneBtn.state = NSControlStateValueOff;
        self.status.stringValue = @"";
    }
}

- (float)toneDbfs {
    static const float lv[] = {-20, -12, -6, -3, 0};
    NSInteger i = self.toneLevel.indexOfSelectedItem; if (i < 0 || i > 4) i = 0;
    return lv[i];
}

- (float)toneHzValue {
    static const float hz[] = {100, 1000, 4000, 10000, 16000};
    NSInteger i = self.toneHz.indexOfSelectedItem; if (i < 0 || i > 4) i = 1;
    return hz[i];
}

- (void)toneLevelChanged:(id)sender {
    if (self.toneBtn.state == NSControlStateValueOn && self.audio.running) {
        [self.audio playTone:YES dbfs:[self toneDbfs] hz:[self toneHzValue]];
        [self logLine:[NSString stringWithFormat:@"tone %.0f Hz at %.0f dBFS", [self toneHzValue], [self toneDbfs]]];
    }
}

- (void)toggleTone:(id)sender {
    if (!self.audio.running) { self.toneBtn.state = NSControlStateValueOff; [self logLine:@"start Monitor first"]; return; }
    BOOL on = self.toneBtn.state == NSControlStateValueOn;
    [self.audio playTone:on dbfs:[self toneDbfs] hz:[self toneHzValue]];
    if (on) [self logLine:[NSString stringWithFormat:@"tone %.0f Hz at %.0f dBFS", [self toneHzValue], [self toneDbfs]]];
}

// --- signal set and grid --------------------------------------------------
- (void)loadSignal {
    NSString *p = [self.root stringByAppendingPathComponent:@"capture/signalset.wav"];
    NSError *e = nil;
    AVAudioFile *f = [[AVAudioFile alloc] initForReading:[NSURL fileURLWithPath:p] error:&e];
    if (!f) { [self logLine:[NSString stringWithFormat:@"no signal set at %@ (run capture/signals.py)", p]]; self.signal = nil; return; }
    AVAudioFormat *mono = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:f.processingFormat.sampleRate channels:1];
    AVAudioPCMBuffer *raw = [[AVAudioPCMBuffer alloc] initWithPCMFormat:f.processingFormat frameCapacity:(AVAudioFrameCount)f.length];
    [f readIntoBuffer:raw error:&e];
    AVAudioPCMBuffer *m = [[AVAudioPCMBuffer alloc] initWithPCMFormat:mono frameCapacity:raw.frameLength];
    m.frameLength = raw.frameLength;
    memcpy(m.floatChannelData[0], raw.floatChannelData[0], raw.frameLength * sizeof(float));
    self.signal = m;
    self.signalVariant = nil;
    NSData *ld = [NSData dataWithContentsOfFile:[self.root stringByAppendingPathComponent:@"capture/layout.json"]];
    if (ld) { NSDictionary *lay = [NSJSONSerialization JSONObjectWithData:ld options:0 error:nil]; if ([lay[@"variant"] isKindOfClass:[NSString class]]) self.signalVariant = lay[@"variant"]; }
    [self logLine:[NSString stringWithFormat:@"signal set: %.1f s at %.0f Hz%@", (double)m.frameLength / mono.sampleRate, mono.sampleRate, self.signalVariant ? [NSString stringWithFormat:@" (variant %@, file names tagged _SET-%@)", self.signalVariant, self.signalVariant] : @""]];
}

- (void)openGrid:(id)sender {
    NSOpenPanel *op = [NSOpenPanel openPanel];
    op.allowedContentTypes = @[[UTType typeWithFilenameExtension:@"json"]];
    op.directoryURL = [NSURL fileURLWithPath:[self.root stringByAppendingPathComponent:@"capture/grids"]];
    if ([op runModal] == NSModalResponseOK) [self loadGrid:op.URL.path];
}

- (void)loadGrid:(NSString *)path {
    NSData *d = [NSData dataWithContentsOfFile:path];
    NSArray *steps = d ? [NSJSONSerialization JSONObjectWithData:d options:0 error:nil] : nil;
    if (![steps isKindOfClass:[NSArray class]]) { [self logLine:[NSString stringWithFormat:@"could not read grid %@", path]]; return; }
    self.grid = [steps mutableCopy];
    self.gridPath = path;
    [[NSUserDefaults standardUserDefaults] setObject:path forKey:@"grid"];
    self.gridLabel.stringValue = [NSString stringWithFormat:@"%@ (%lu steps)", path.lastPathComponent, (unsigned long)self.grid.count];
    // A row that repeats an earlier row's settings (a flat reference at the
    // start and the end, for drift) is done only when ITS take exists: the
    // n-th occurrence of a file name is take n (`_take2`, ...), which is what
    // -nextAvailableFileNameFor: writes. Build 9, 2026-09-23 -- before this a
    // closing flat row showed as done because the opening one was.
    self.doneFiles = [NSMutableArray array];
    NSMutableDictionary<NSString *, NSNumber *> *seen = [NSMutableDictionary dictionary];
    for (NSDictionary *s in self.grid) {
        NSString *base = [self fileNameFor:s];
        const NSInteger n = seen[base].integerValue + 1;
        seen[base] = @(n);
        NSString *name = n == 1 ? base : [NSString stringWithFormat:@"%@_take%ld.%@", [base stringByDeletingPathExtension], (long)n, [base pathExtension]];
        NSString *f = [self.outDir stringByAppendingPathComponent:name];
        [self.doneFiles addObject:[[NSFileManager defaultManager] fileExistsAtPath:f] ? f : (id)[NSNull null]];
    }
    // Grid rule (Mark, 2026-09-23): every MAX row after every LIN row, so
    // the input pot is calibrated once and turned up once.
    BOOL seenMax = NO;
    for (NSUInteger i = 0; i < self.grid.count; i++) {
        const BOOL isMax = [[self.grid[i][@"input"] description] isEqualToString:@"MAX"];
        if (seenMax && !isMax) {
            [self logLine:[NSString stringWithFormat:@"WARNING: step %lu is LIN after a MAX step. MAX rows belong at the end of the grid.", (unsigned long)i + 1]];
            break;
        }
        seenMax = seenMax || isMax;
    }
    [self.table reloadData];
    [self selectNext];
}

- (NSString *)fileNameFor:(NSDictionary *)step {
    NSMutableString *n = [NSMutableString stringWithFormat:@"AX30G_%@", step[@"effect"]];
    NSDictionary *p = step[@"params"];
    // keep the grid's own key order (JSON objects are ordered in the file; NSDictionary is not), so sort by the key order in the file text
    for (NSString *k in [self orderedKeys:p]) {
        NSString *ks = [k stringByReplacingOccurrencesOfString:@" " withString:@""];
        [n appendFormat:@"_%@-%@", ks, p[k]];
    }
    [n appendFormat:@"_IN-%@", step[@"input"] ?: @"N"];
    if (self.signalVariant.length) [n appendFormat:@"_SET-%@", self.signalVariant];
    [n appendString:@".wav"];
    return n;
}

- (NSString *)nextAvailableFileNameFor:(NSDictionary *)step take:(NSInteger *)takeOut {
    // `fileNameFor:` if nothing at outDir/name exists yet, else the same name
    // with `_take2`, `_take3`, ... inserted before the extension -- so
    // re-capturing an already-done grid row never silently overwrites the
    // previous file (three REV time-invariance captures were lost this way
    // 2026-09-17). `fileNameFor:` itself stays the canonical name used for
    // the done-checkmark and the High-Damp/Feedback reference lookup.
    NSString *base = [self fileNameFor:step];
    NSFileManager *fm = [NSFileManager defaultManager];
    if (![fm fileExistsAtPath:[self.outDir stringByAppendingPathComponent:base]]) {
        if (takeOut) *takeOut = 1;
        return base;
    }
    NSString *stem = [base stringByDeletingPathExtension];
    NSString *ext = [base pathExtension];
    NSInteger take = 2;
    NSString *name;
    do {
        name = [NSString stringWithFormat:@"%@_take%ld.%@", stem, (long)take, ext];
        if (![fm fileExistsAtPath:[self.outDir stringByAppendingPathComponent:name]]) break;
        take++;
    } while (YES);
    if (takeOut) *takeOut = take;
    return name;
}

- (NSArray *)orderedKeys:(NSDictionary *)p {
    // canonical order shared with capture.py: as listed in the grid file; fall back to a fixed order
    NSArray *canon = @[@"L Dly", @"R Dly", @"Dly Time", @"L Fb", @"R Fb", @"Feedback", @"High Damp", @"Speed", @"Depth",
                       @"L Bal", @"R Bal", @"Balance", @"Type", @"Pre Dly", @"Rev Time", @"Ducking"];
    NSMutableArray *out = [NSMutableArray array];
    for (NSString *k in canon) if (p[k]) [out addObject:k];
    for (NSString *k in [p.allKeys sortedArrayUsingSelector:@selector(compare:)]) if (![out containsObject:k]) [out addObject:k];
    return out;
}

// The order the UNIT's own edit pages show each effect's parameters
// (owner's manual, docs/chain-rules-2026-09-17.md section 4), in the grid
// files' key names. Display only: file names keep -orderedKeys: so existing
// captures still match their grid rows. (Build 7, 2026-09-23: the HYPR grid's
// nine parameters were shown alphabetically, not in the unit's order.)
- (NSArray *)unitOrderedKeys:(NSDictionary *)p effect:(NSString *)effect {
    static NSDictionary *order;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        order = @{
            @"COMP": @[@"Sensitivity", @"Level", @"Attack"],
            @"3BEQ": @[@"Bass", @"Mid Freq", @"Mid Gain", @"Treble", @"Trim", @"Trim Gain"],
            @"HYPR": @[@"Type", @"Harmonics", @"Sensitivity", @"Polarity", @"Depth", @"Decay", @"Resonance", @"Direct Level", @"Effect Level"],
            @"CHO":  @[@"Speed", @"Depth"],
            @"MODD": @[@"Speed", @"Depth", @"Dly Time", @"Feedback", @"High Damp", @"L Bal", @"R Bal"],
            @"SMOD": @[@"Speed", @"Depth", @"L Dly", @"R Dly", @"L Fb", @"R Fb", @"L Bal", @"R Bal"],
            @"SDLY": @[@"L Dly", @"R Dly", @"L Fb", @"R Fb", @"High Damp", @"L Bal", @"R Bal", @"Ducking"],
            @"XDLY": @[@"L Dly", @"R Dly", @"L Fb", @"R Fb", @"High Damp", @"Balance", @"Ducking"],
            @"HDLY": @[@"Dly Time", @"Feedback", @"High Damp", @"L Bal", @"R Bal", @"Ducking"],
            @"REV":  @[@"Type", @"Pre Dly", @"Rev Time", @"High Damp", @"Balance"],
        };
    });
    NSMutableArray *out = [NSMutableArray array];
    for (NSString *k in order[effect ?: @""]) if (p[k]) [out addObject:k];
    for (NSString *k in [self orderedKeys:p]) if (![out containsObject:k]) [out addObject:k];
    return out;
}

- (NSString *)paramsLine:(NSDictionary *)step {
    NSMutableArray *a = [NSMutableArray array];
    NSDictionary *p = step[@"params"];
    for (NSString *k in [self unitOrderedKeys:p effect:step[@"effect"]]) [a addObject:[NSString stringWithFormat:@"%@ %@", k, p[k]]];
    return [a componentsJoinedByString:@"   "];
}

- (void)selectNext {
    self.current = -1;
    for (NSInteger i = 0; i < (NSInteger)self.grid.count; i++) if (self.doneFiles[i] == (id)[NSNull null]) { self.current = i; break; }
    if (self.current < 0) { self.stepText.stringValue = @"Grid complete."; [self.table deselectAll:nil]; return; }
    [self.table selectRowIndexes:[NSIndexSet indexSetWithIndex:self.current] byExtendingSelection:NO];
    [self.table scrollRowToVisible:self.current];
    [self showStep];
}

// What the unit is set to right now, as far as the app knows: the step whose
// Capture was last pressed (Mark sets the unit before pressing it, so this
// holds even if the capture then fails). Persisted, so it survives a relaunch.
- (NSDictionary *)unitState {
    NSDictionary *d = [[NSUserDefaults standardUserDefaults] dictionaryForKey:@"unitState"];
    return [d isKindOfClass:[NSDictionary class]] ? d : nil;
}

- (void)setUnitStateFromStep:(NSDictionary *)step {
    NSMutableDictionary *d = [NSMutableDictionary dictionary];
    d[@"effect"] = step[@"effect"] ?: @"";
    d[@"input"] = step[@"input"] ?: @"N";
    d[@"params"] = step[@"params"] ?: @{};
    [[NSUserDefaults standardUserDefaults] setObject:d forKey:@"unitState"];
}

// The step card: only the parameters that differ from what the unit is set
// to now, big and in the unit's own order, then everything that stays as a
// quieter checklist. A different effect (or no known state) lists every
// parameter as a change.
- (void)showStep {
    if (self.current < 0 || self.current >= (NSInteger)self.grid.count) return;
    NSDictionary *s = self.grid[self.current];
    NSDictionary *p = s[@"params"];
    NSString *input = s[@"input"] ?: @"N";
    NSDictionary *was = [self unitState];
    const BOOL sameEffect = was && [was[@"effect"] isEqual:s[@"effect"]];
    NSDictionary *wasP = sameEffect ? was[@"params"] : nil;

    NSFont *mono = [NSFont monospacedSystemFontOfSize:14 weight:NSFontWeightRegular];
    NSFont *big = [NSFont monospacedSystemFontOfSize:18 weight:NSFontWeightSemibold];
    NSDictionary *hdr = @{NSFontAttributeName: [NSFont systemFontOfSize:14 weight:NSFontWeightSemibold], NSForegroundColorAttributeName: NSColor.labelColor};
    NSDictionary *chg = @{NSFontAttributeName: big, NSForegroundColorAttributeName: NSColor.systemOrangeColor};
    NSDictionary *dim = @{NSFontAttributeName: mono, NSForegroundColorAttributeName: NSColor.secondaryLabelColor};
    NSMutableAttributedString *t = [[NSMutableAttributedString alloc] init];
    void (^add)(NSString *, NSDictionary *) = ^(NSString *str, NSDictionary *a) {
        [t appendAttributedString:[[NSAttributedString alloc] initWithString:str attributes:a]];
    };

    add([NSString stringWithFormat:@"Step %ld of %lu — %@, Input Level %@\n", (long)self.current + 1, (unsigned long)self.grid.count, s[@"effect"], input], hdr);
    NSArray *keys = [self unitOrderedKeys:p effect:s[@"effect"]];
    NSMutableArray *changed = [NSMutableArray array], *same = [NSMutableArray array];
    for (NSString *k in keys) {
        if (wasP && wasP[k] && [[wasP[k] description] isEqualToString:[p[k] description]]) [same addObject:k];
        else [changed addObject:k];
    }
    const BOOL inputChanged = sameEffect && ![[was[@"input"] description] isEqualToString:input];
    NSUInteger w = 0;
    for (NSString *k in changed) w = MAX(w, k.length);
    // Big type for a handful of changes; a full list (new effect, nine HYPR
    // parameters) at the regular size so it fits the card.
    if (changed.count > 5) chg = @{NSFontAttributeName: [NSFont monospacedSystemFontOfSize:14 weight:NSFontWeightSemibold], NSForegroundColorAttributeName: NSColor.systemOrangeColor};
    if (!sameEffect) add(was ? [NSString stringWithFormat:@"New effect — set every parameter:\n"] : @"Set every parameter:\n", dim);
    else if (!changed.count && !inputChanged) add(@"\nNo changes from the last capture (a repeat).\n", chg);
    else add(@"Change:\n", dim);
    for (NSString *k in changed) {
        NSString *pad = [k stringByPaddingToLength:w withString:@" " startingAtIndex:0];
        if (wasP && wasP[k]) add([NSString stringWithFormat:@"  %@  %@ → %@\n", pad, wasP[k], p[k]], chg);
        else add([NSString stringWithFormat:@"  %@  %@\n", pad, p[k]], chg);
    }
    if (inputChanged) add([NSString stringWithFormat:@"  Input Level  %@ → %@\n", was[@"input"], input], chg);
    if (same.count) {
        NSMutableArray *a = [NSMutableArray array];
        for (NSString *k in same) [a addObject:[NSString stringWithFormat:@"%@ %@", k, p[k]]];
        add([NSString stringWithFormat:@"\nUnchanged: %@\n", [a componentsJoinedByString:@" · "]], dim);
    }
    add(@"\nSet the unit, then press Capture (Return).", dim);
    self.stepText.attributedStringValue = t;
}

- (void)tableViewSelectionDidChange:(NSNotification *)n {
    if (self.table.selectedRow >= 0) { self.current = self.table.selectedRow; [self showStep]; }
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)t { return self.grid.count; }
- (id)tableView:(NSTableView *)t objectValueForTableColumn:(NSTableColumn *)c row:(NSInteger)r {
    NSDictionary *s = self.grid[r];
    NSString *id_ = c.identifier;
    if ([id_ isEqualToString:@"n"]) return @(r + 1);
    if ([id_ isEqualToString:@"done"]) return self.doneFiles[r] == (id)[NSNull null] ? @"" : @"✓";
    if ([id_ isEqualToString:@"effect"]) return s[@"effect"];
    if ([id_ isEqualToString:@"in"]) return s[@"input"] ?: @"N";
    return [self paramsLine:s];
}

- (void)skip:(id)sender {
    if (self.current < 0) return;
    NSInteger next = self.current + 1;
    while (next < (NSInteger)self.grid.count && self.doneFiles[next] != (id)[NSNull null]) next++;
    if (next >= (NSInteger)self.grid.count) { [self selectNext]; return; }
    self.current = next;
    [self.table selectRowIndexes:[NSIndexSet indexSetWithIndex:next] byExtendingSelection:NO];
    [self showStep];
}

// --- capture --------------------------------------------------------------
- (void)capture:(id)sender {
    if (self.current < 0 || !self.grid.count) { [self logLine:@"open a grid first"]; return; }
    if (!self.audio.running) { [self logLine:@"start Monitor first"]; return; }
    if (!self.signal) { [self logLine:@"no signal set"]; return; }
    NSDictionary *step = self.grid[self.current];
    [self setUnitStateFromStep:step];   // the unit is now set to this step, whatever happens to the capture
    NSInteger take = 1;
    NSString *name = [self nextAvailableFileNameFor:step take:&take];
    [[NSFileManager defaultManager] createDirectoryAtPath:self.outDir withIntermediateDirectories:YES attributes:nil error:nil];
    NSString *path = [self.outDir stringByAppendingPathComponent:name];
    NSInteger row = self.current;
    self.captureBtn.enabled = NO;
    self.toneBtn.state = NSControlStateValueOff; [self.audio playTone:NO];
    [self applyChannels];
    for (AXMeter *m in @[self.mL, self.mR, self.mRef]) m.hold = 0;
    [self logLine:[NSString stringWithFormat:@"capturing %@%@", name, take > 1 ? [NSString stringWithFormat:@" (take %ld)", (long)take] : @""]];
    NSError *e = nil;
    BOOL ok = [self.audio playrecSignal:self.signal tailSeconds:3.0 toURL:[NSURL fileURLWithPath:path] done:^(NSError *err) {
        self.captureBtn.enabled = YES;
        if (err) { [self logLine:[NSString stringWithFormat:@"capture failed: %@", err.localizedDescription]]; return; }
        float pk = fmaxf(self.mL.hold, self.mR.hold);
        [self logLine:[NSString stringWithFormat:@"done: peak L/R %.1f dBFS, ref %.1f dBFS%@", 20 * log10f(fmaxf(pk, 1e-6)), 20 * log10f(fmaxf(self.mRef.hold, 1e-6)), pk >= 0.99 ? @"  ** CLIPPED **" : @""]];
        self.doneFiles[row] = path;
        [self appendLog:step file:name];
        [self.table reloadData];
        [self analyze:path step:step];
        [self selectNext];
    } error:&e];
    if (!ok) { self.captureBtn.enabled = YES; [self logLine:[NSString stringWithFormat:@"could not start capture: %@", e.localizedDescription]]; }
}

- (void)abort:(id)sender { [self.audio abort]; }

- (void)appendLog:(NSDictionary *)step file:(NSString *)name {
    NSString *log = [self.outDir stringByAppendingPathComponent:@"capture_log.csv"];
    NSFileManager *fm = [NSFileManager defaultManager];
    if (![fm fileExistsAtPath:log]) [@"timestamp,file,effect,input,params_json,device,note\n" writeToFile:log atomically:YES encoding:NSUTF8StringEncoding error:nil];
    NSData *pj = [NSJSONSerialization dataWithJSONObject:step[@"params"] options:0 error:nil];
    NSString *ps = [[[NSString alloc] initWithData:pj encoding:NSUTF8StringEncoding] stringByReplacingOccurrencesOfString:@"\"" withString:@"\"\""];
    NSISO8601DateFormatter *f = [NSISO8601DateFormatter new];
    NSString *line = [NSString stringWithFormat:@"%@,%@,%@,%@,\"%@\",%@,bench build %d\n", [f stringFromDate:[NSDate date]], name, step[@"effect"], step[@"input"] ?: @"N", ps, self.devPop.titleOfSelectedItem, AX_BUILD];
    NSFileHandle *h = [NSFileHandle fileHandleForWritingAtPath:log];
    [h seekToEndOfFile]; [h writeData:[line dataUsingEncoding:NSUTF8StringEncoding]]; [h closeFile];
}

// --- analysis (Python) ----------------------------------------------------
- (NSString *)referenceFor:(NSDictionary *)step key:(NSString *)key {
    // a done capture with the same params except `key` = 0
    NSMutableDictionary *want = [step[@"params"] mutableCopy];
    if (!want[key] || [want[key] doubleValue] == 0) return nil;
    want[key] = @0;
    NSDictionary *probe = @{@"effect": step[@"effect"], @"params": want, @"input": step[@"input"] ?: @"N"};
    NSString *f = [self.outDir stringByAppendingPathComponent:[self fileNameFor:probe]];
    return [[NSFileManager defaultManager] fileExistsAtPath:f] ? f : nil;
}

- (void)analyze:(NSString *)path step:(NSDictionary *)step {
    NSString *py = [self.root stringByAppendingPathComponent:@".venv/bin/python"];
    NSString *run = [self.root stringByAppendingPathComponent:@"analysis/run.py"];
    if (![[NSFileManager defaultManager] fileExistsAtPath:py]) { [self logLine:@"no .venv in the project (make venv)"]; return; }
    NSMutableArray *args = [NSMutableArray arrayWithObjects:run, path, @"--effect", step[@"effect"], nil];
    NSString *effect = step[@"effect"];
    NSString *ref = nil;
    if ([@[@"SDLY", @"XDLY", @"HDLY"] containsObject:effect]) { if ((ref = [self referenceFor:step key:@"High Damp"])) [args addObjectsFromArray:@[@"--ref-damp0", ref]]; }
    else { if ((ref = [self referenceFor:step key:@"Feedback"])) [args addObjectsFromArray:@[@"--ref-fb0", ref]]; }
    NSTask *t = [NSTask new];
    t.executableURL = [NSURL fileURLWithPath:py];
    t.arguments = args;
    t.currentDirectoryURL = [NSURL fileURLWithPath:self.root];
    NSPipe *out = [NSPipe pipe], *err = [NSPipe pipe];
    t.standardOutput = out; t.standardError = err;
    [self logLine:[NSString stringWithFormat:@"analysis: %@%@", effect, ref ? [NSString stringWithFormat:@" (with reference %@)", ref.lastPathComponent] : @""]];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        NSError *e = nil;
        if (![t launchAndReturnError:&e]) { dispatch_async(dispatch_get_main_queue(), ^{ [self logLine:[NSString stringWithFormat:@"analysis failed to start: %@", e.localizedDescription]]; }); return; }
        NSData *od = [out.fileHandleForReading readDataToEndOfFile];
        NSData *ed = [err.fileHandleForReading readDataToEndOfFile];
        [t waitUntilExit];
        NSString *o = [[NSString alloc] initWithData:od encoding:NSUTF8StringEncoding] ?: @"";
        NSString *es = [[NSString alloc] initWithData:ed encoding:NSUTF8StringEncoding] ?: @"";
        dispatch_async(dispatch_get_main_queue(), ^{
            NSArray *lines = [[o stringByTrimmingCharactersInSet:[NSCharacterSet newlineCharacterSet]] componentsSeparatedByString:@"\n"];
            if (t.terminationStatus != 0) {
                NSArray *el = [[es stringByTrimmingCharactersInSet:[NSCharacterSet newlineCharacterSet]] componentsSeparatedByString:@"\n"];
                [self logLine:[NSString stringWithFormat:@"analysis error: %@", el.lastObject ?: @"?"]];
                return;
            }
            for (NSString *l in lines) if (![l hasSuffix:@".png"] && l.length) [self logLine:[NSString stringWithFormat:@"  %@", l]];
            NSString *png = lines.lastObject;
            if ([png hasSuffix:@".png"]) self.picture.image = [[NSImage alloc] initWithContentsOfFile:png];
        });
    });
}

// --- folders --------------------------------------------------------------
- (void)chooseOutDir:(id)sender {
    NSOpenPanel *op = [NSOpenPanel openPanel];
    op.canChooseDirectories = YES; op.canChooseFiles = NO; op.canCreateDirectories = YES;
    op.directoryURL = [NSURL fileURLWithPath:self.outDir];
    if ([op runModal] == NSModalResponseOK) {
        self.outDir = op.URL.path; self.outDirLabel.stringValue = self.outDir;
        [[NSUserDefaults standardUserDefaults] setObject:self.outDir forKey:@"outDir"];
        if (self.gridPath) [self loadGrid:self.gridPath];
    }
}

- (void)chooseRoot:(id)sender {
    NSOpenPanel *op = [NSOpenPanel openPanel];
    op.canChooseDirectories = YES; op.canChooseFiles = NO;
    op.directoryURL = [NSURL fileURLWithPath:self.root];
    op.message = @"The ax30g project folder (has capture/, analysis/, .venv/)";
    if ([op runModal] == NSModalResponseOK) {
        self.root = op.URL.path;
        [[NSUserDefaults standardUserDefaults] setObject:self.root forKey:@"root"];
        [self logLine:[NSString stringWithFormat:@"project: %@", self.root]];
        [self loadSignal];
    }
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)a { return YES; }
- (void)applicationWillTerminate:(NSNotification *)n { [self.audio stop]; }
@end
