#import "AXAudio.h"

@implementation AXDevice
@end

static NSInteger channelCount(AudioDeviceID dev, AudioObjectPropertyScope scope) {
    AudioObjectPropertyAddress a = {kAudioDevicePropertyStreamConfiguration, scope, kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(dev, &a, 0, NULL, &size) != noErr) return 0;
    AudioBufferList *bl = malloc(size);
    NSInteger n = 0;
    if (AudioObjectGetPropertyData(dev, &a, 0, NULL, &size, bl) == noErr)
        for (UInt32 i = 0; i < bl->mNumberBuffers; i++) n += bl->mBuffers[i].mNumberChannels;
    free(bl);
    return n;
}

@interface AXAudio ()
@property AVAudioEngine *engine;
@property AVAudioPlayerNode *player;
@property AVAudioFile *recFile;
@property BOOL recording, toneOn;
@property AXDoneBlock doneBlock;
@property double stopAt;         // host seconds
@property NSInteger outChannels;
@end

@implementation AXAudio

+ (NSArray<AXDevice *> *)devices {
    NSMutableArray *out = [NSMutableArray array];
    AudioObjectPropertyAddress a = {kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, NULL, &size) != noErr) return out;
    UInt32 n = size / sizeof(AudioDeviceID);
    AudioDeviceID *ids = malloc(size);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &size, ids);
    for (UInt32 i = 0; i < n; i++) {
        AXDevice *d = [AXDevice new];
        d.deviceID = ids[i];
        CFStringRef name = NULL;
        UInt32 ns = sizeof(name);
        AudioObjectPropertyAddress na = {kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
        if (AudioObjectGetPropertyData(ids[i], &na, 0, NULL, &ns, &name) == noErr && name) {
            d.name = (__bridge_transfer NSString *)name;
        } else d.name = [NSString stringWithFormat:@"device %u", ids[i]];
        d.inputs = channelCount(ids[i], kAudioObjectPropertyScopeInput);
        d.outputs = channelCount(ids[i], kAudioObjectPropertyScopeOutput);
        if (d.inputs > 0 && d.outputs > 0) [out addObject:d];
    }
    free(ids);
    return out;
}

static BOOL setDevice(AudioUnit au, AudioDeviceID dev) {
    return AudioUnitSetProperty(au, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &dev, sizeof(dev)) == noErr;
}

static BOOL setNominalRate(AudioDeviceID dev, double fs) {
    AudioObjectPropertyAddress a = {kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    Float64 cur = 0; UInt32 sz = sizeof(cur);
    AudioObjectGetPropertyData(dev, &a, 0, NULL, &sz, &cur);
    if (fabs(cur - fs) < 1) return YES;
    Float64 want = fs;
    if (AudioObjectSetPropertyData(dev, &a, 0, NULL, sizeof(want), &want) != noErr) return NO;
    for (int i = 0; i < 40; i++) {   // the change is asynchronous
        usleep(50000);
        AudioObjectGetPropertyData(dev, &a, 0, NULL, &sz, &cur);
        if (fabs(cur - fs) < 1) return YES;
    }
    return NO;
}

- (BOOL)startWithDevice:(AXDevice *)dev sampleRate:(double)fs error:(NSError **)err {
    [self stop];
    if (fs > 0 && !setNominalRate(dev.deviceID, fs)) {
        if (err) *err = [NSError errorWithDomain:@"AX" code:1 userInfo:@{NSLocalizedDescriptionKey:
            [NSString stringWithFormat:@"could not set %@ to %.0f Hz", dev.name, fs]}];
        return NO;
    }
    self.engine = [AVAudioEngine new];
    if (!setDevice(self.engine.outputNode.audioUnit, dev.deviceID) || !setDevice(self.engine.inputNode.audioUnit, dev.deviceID)) {
        if (err) *err = [NSError errorWithDomain:@"AX" code:2 userInfo:@{NSLocalizedDescriptionKey: @"could not select the device on the engine"}];
        return NO;
    }
    self.player = [AVAudioPlayerNode new];
    [self.engine attachNode:self.player];
    AVAudioFormat *outFmt = [self.engine.outputNode outputFormatForBus:0];
    self.outChannels = outFmt.channelCount;
    _sampleRate = outFmt.sampleRate;
    [self.engine connect:self.player to:self.engine.outputNode format:outFmt];
    AVAudioFormat *inFmt = [self.engine.inputNode outputFormatForBus:0];
    __weak AXAudio *w = self;
    [self.engine.inputNode installTapOnBus:0 bufferSize:2048 format:inFmt block:^(AVAudioPCMBuffer *buf, AVAudioTime *when) {
        AXAudio *s = w; if (!s) return;
        NSArray *chs = s.inputChannels;
        NSInteger n = chs.count;
        float peaks[8] = {0};
        AVAudioPCMBuffer *rec = nil;
        if (s.recording && s.recFile) {
            rec = [[AVAudioPCMBuffer alloc] initWithPCMFormat:s.recFile.processingFormat frameCapacity:buf.frameLength];
            rec.frameLength = buf.frameLength;
        }
        for (NSInteger c = 0; c < n && c < 8; c++) {
            NSInteger ch = [chs[c] integerValue];
            if (ch < 0 || ch >= (NSInteger)buf.format.channelCount) continue;
            const float *src = buf.floatChannelData[ch];
            float pk = 0;
            for (AVAudioFrameCount i = 0; i < buf.frameLength; i++) { float v = fabsf(src[i]); if (v > pk) pk = v; }
            peaks[c] = pk;
            if (rec && c < (NSInteger)rec.format.channelCount) memcpy(rec.floatChannelData[c], src, buf.frameLength * sizeof(float));
        }
        if (rec) {
            NSError *e = nil;
            [s.recFile writeFromBuffer:rec error:&e];
            double now = [[NSDate date] timeIntervalSince1970];
            if (now >= s.stopAt) [s finishRecording:nil];
        }
        if (s.levelBlock) {
            NSData *copy = [NSData dataWithBytes:peaks length:sizeof(peaks)];
            dispatch_async(dispatch_get_main_queue(), ^{ s.levelBlock((float *)copy.bytes, n); });
        }
    }];
    NSError *e = nil;
    if (![self.engine startAndReturnError:&e]) { if (err) *err = e; return NO; }
    _running = YES;
    return YES;
}

- (void)stop {
    if (!self.engine) return;
    [self.engine.inputNode removeTapOnBus:0];
    [self.player stop];
    [self.engine stop];
    self.engine = nil;
    self.player = nil;
    _running = NO;
}

- (AVAudioPCMBuffer *)resampled:(AVAudioPCMBuffer *)mono to:(double)fs {
    if (fabs(mono.format.sampleRate - fs) < 1) return mono;
    AVAudioFormat *dst = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:fs channels:1];
    AVAudioConverter *cv = [[AVAudioConverter alloc] initFromFormat:mono.format toFormat:dst];
    AVAudioFrameCount n = (AVAudioFrameCount)ceil(mono.frameLength * fs / mono.format.sampleRate) + 64;
    AVAudioPCMBuffer *out = [[AVAudioPCMBuffer alloc] initWithPCMFormat:dst frameCapacity:n];
    __block BOOL fed = NO;
    NSError *e = nil;
    [cv convertToBuffer:out error:&e withInputFromBlock:^AVAudioBuffer *(AVAudioPacketCount inNumberOfPackets, AVAudioConverterInputStatus *outStatus) {
        if (fed) { *outStatus = AVAudioConverterInputStatus_EndOfStream; return nil; }
        fed = YES; *outStatus = AVAudioConverterInputStatus_HaveData; return mono;
    }];
    return out;
}

- (AVAudioPCMBuffer *)outputBufferFrom:(AVAudioPCMBuffer *)mono gainDb:(float)gainDb {
    mono = [self resampled:mono to:self.sampleRate];
    AVAudioFormat *fmt = [self.engine.outputNode outputFormatForBus:0];
    AVAudioPCMBuffer *b = [[AVAudioPCMBuffer alloc] initWithPCMFormat:fmt frameCapacity:mono.frameLength];
    b.frameLength = mono.frameLength;
    for (NSInteger c = 0; c < (NSInteger)fmt.channelCount; c++) memset(b.floatChannelData[c], 0, mono.frameLength * sizeof(float));
    const float *src = mono.floatChannelData[0];
    if (gainDb == 0) {
        for (NSInteger c = self.outputPairBase; c < self.outputPairBase + 2 && c < (NSInteger)fmt.channelCount; c++)
            memcpy(b.floatChannelData[c], src, mono.frameLength * sizeof(float));
        return b;
    }
    float gain = powf(10.0f, gainDb / 20.0f);
    for (NSInteger c = self.outputPairBase; c < self.outputPairBase + 2 && c < (NSInteger)fmt.channelCount; c++) {
        float *dst = b.floatChannelData[c];
        for (AVAudioFrameCount i = 0; i < mono.frameLength; i++) dst[i] = fmaxf(-1.0f, fminf(1.0f, src[i] * gain));
    }
    return b;
}

- (BOOL)playrecSignal:(AVAudioPCMBuffer *)signal sendDb:(float)sendDb tailSeconds:(double)tail toURL:(NSURL *)outURL done:(AXDoneBlock)done error:(NSError **)err {
    if (!self.running || self.recording) return NO;
    NSDictionary *settings = @{AVFormatIDKey: @(kAudioFormatLinearPCM), AVSampleRateKey: @(self.sampleRate),
                               AVNumberOfChannelsKey: @(self.inputChannels.count), AVLinearPCMBitDepthKey: @32,
                               AVLinearPCMIsFloatKey: @YES, AVLinearPCMIsNonInterleaved: @NO};
    NSError *e = nil;
    self.recFile = [[AVAudioFile alloc] initForWriting:outURL settings:settings commonFormat:AVAudioPCMFormatFloat32 interleaved:NO error:&e];
    if (!self.recFile) { if (err) *err = e; return NO; }
    self.doneBlock = done;
    double dur = (double)signal.frameLength / signal.format.sampleRate;
    self.stopAt = [[NSDate date] timeIntervalSince1970] + 0.5 + dur + tail;
    self.recording = YES;                       // recording starts first; the reference channel aligns it
    AVAudioPCMBuffer *ob = [self outputBufferFrom:signal gainDb:sendDb];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.5 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        if (!self.recording) return;
        [self.player scheduleBuffer:ob completionHandler:nil];
        [self.player play];
    });
    return YES;
}

- (void)finishRecording:(NSError *)error {
    if (!self.recording) return;
    self.recording = NO;
    self.recFile = nil;   // closes the file
    AXDoneBlock d = self.doneBlock;
    self.doneBlock = nil;
    dispatch_async(dispatch_get_main_queue(), ^{ [self.player stop]; if (d) d(error); });
}

- (void)abort {
    [self finishRecording:[NSError errorWithDomain:@"AX" code:3 userInfo:@{NSLocalizedDescriptionKey: @"aborted"}]];
}

- (void)playTone:(BOOL)on { [self playTone:on dbfs:-20.0f]; }

- (void)playTone:(BOOL)on dbfs:(float)dbfs { [self playTone:on dbfs:dbfs hz:1000.0f]; }

- (void)playTone:(BOOL)on dbfs:(float)dbfs hz:(float)hz {
    if (!self.running) return;
    self.toneOn = on;
    [self.player stop];
    if (!on) return;
    double fs = self.sampleRate;
    AVAudioFormat *mono = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:fs channels:1];
    AVAudioFrameCount n = (AVAudioFrameCount)(fs * 2);
    AVAudioPCMBuffer *m = [[AVAudioPCMBuffer alloc] initWithPCMFormat:mono frameCapacity:n];
    m.frameLength = n;
    float amp = powf(10, dbfs / 20);
    // integer number of cycles in the 2 s loop so the seam is silent
    double cyc = round(hz * 2.0), w = 2 * M_PI * cyc / n;
    for (AVAudioFrameCount i = 0; i < n; i++) m.floatChannelData[0][i] = amp * sinf(w * i);
    AVAudioPCMBuffer *ob = [self outputBufferFrom:m gainDb:0];
    [self.player scheduleBuffer:ob atTime:nil options:AVAudioPlayerNodeBufferLoops completionHandler:nil];
    [self.player play];
}

@end
