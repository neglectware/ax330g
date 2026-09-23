// Audio I/O for the bench: one CoreAudio device does both directions, so the
// play and the record share a clock and the reference channel fixes the rest.
#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreAudio/CoreAudio.h>

@interface AXDevice : NSObject
@property AudioDeviceID deviceID;
@property (copy) NSString *name;
@property NSInteger inputs, outputs;
@end

typedef void (^AXLevelBlock)(float *peaks, NSInteger n);   // peaks for the selected input channels
typedef void (^AXDoneBlock)(NSError *error);

@interface AXAudio : NSObject
+ (NSArray<AXDevice *> *)devices;
@property (readonly) BOOL running;
@property (readonly) double sampleRate;
@property (copy) AXLevelBlock levelBlock;
@property NSArray<NSNumber *> *inputChannels;    // 0-based, [L, R, ref]
@property NSInteger outputPairBase;              // 0-based first channel of the output pair

- (BOOL)startWithDevice:(AXDevice *)dev sampleRate:(double)fs error:(NSError **)err;
- (void)stop;
// Play `signal` (mono float, fs samples/s) on the output pair while writing the
// selected input channels to `outURL` (3-channel float WAV). Calls done on the main thread.
// `sendDb` scales the played signal (0 = unity; the reference channel is an
// analog loopback off the same output, so it records whatever was actually
// sent) -- samples are clamped to ±1.0 after scaling.
- (BOOL)playrecSignal:(AVAudioPCMBuffer *)signal sendDb:(float)sendDb tailSeconds:(double)tail toURL:(NSURL *)outURL done:(AXDoneBlock)done error:(NSError **)err;
- (void)playTone:(BOOL)on;      // 1 kHz at -20 dBFS on the output pair, for calibration
- (void)playTone:(BOOL)on dbfs:(float)dbfs;  // same, at a chosen level
- (void)playTone:(BOOL)on dbfs:(float)dbfs hz:(float)hz;  // chosen level and frequency
- (void)abort;
@end
