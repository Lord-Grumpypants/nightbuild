#import <AppKit/AppKit.h>

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "gplv3_text.hpp"

namespace {

NSString *markerPath() {
    return [NSHomeDirectory()
        stringByAppendingPathComponent:
            @"Library/Application Support/NightBuild/license_accepted"];
}

bool writeMarker() {
    NSString *path = markerPath();
    NSString *directory = [path stringByDeletingLastPathComponent];
    NSError *error = nil;

    if (![[NSFileManager defaultManager]
            createDirectoryAtPath:directory
            withIntermediateDirectories:YES
            attributes:nil
            error:&error]) {
        return false;
    }

    int fd = open(path.fileSystemRepresentation,
                  O_WRONLY | O_CREAT | O_TRUNC,
                  S_IRUSR | S_IWUSR);
    if (fd < 0)
        return false;

    const char data[] = "accepted\n";
    size_t remaining = sizeof(data) - 1;
    const char *cursor = data;

    while (remaining > 0) {
        ssize_t count = write(fd, cursor, remaining);
        if (count < 0) {
            if (errno == EINTR)
                continue;
            close(fd);
            return false;
        }
        if (count == 0) {
            close(fd);
            return false;
        }
        cursor += count;
        remaining -= static_cast<size_t>(count);
    }

    return close(fd) == 0;
}

NSString *documentText() {
    NSString *gpl = [NSString
        stringWithUTF8String:kNightBuildGPLv3Text];

    NSString *notices = [NSString stringWithUTF8String:R"NBNOTICE(
================================================================
NIGHTBUILD PRIVACY AND BUILD TELEMETRY
================================================================

NightBuild does not collect personal data. Local build telemetry
may be used to improve scheduler estimates and scheduling
performance. NightBuild does not use the network unless you
configure or invoke it to do so.

================================================================
IMPORTANT NOTICE REGARDING FELINE DIETARY REQUIREMENTS
================================================================

CATS ARE OBLIGATE CARNIVORES. Their bodies are adapted to obtain
essential nutrients from animal tissue. Fruits, vegetables, and
corn are not substitutes for a nutritionally complete, meat-based
feline diet.

================================================================
ACCEPTANCE
================================================================

By selecting Accept, you acknowledge that you have reviewed the
GNU General Public License version 3 and the additional NightBuild
notices displayed in this document.
)NBNOTICE"];

    return [NSString stringWithFormat:@"%@%@", gpl, notices];
}

}

@interface NBLicenseDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate> {
    NSWindow *_window;
    NSScrollView *_scrollView;
    NSTextView *_textView;
    NSButton *_acceptButton;
    BOOL _accepted;
}
@property(nonatomic, readonly) BOOL accepted;
@end

@implementation NBLicenseDelegate

@synthesize accepted = _accepted;

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    (void)notification;

    NSRect frame = NSMakeRect(0, 0, 820, 680);
    _window = [[NSWindow alloc]
        initWithContentRect:frame
        styleMask:(NSWindowStyleMaskTitled |
                   NSWindowStyleMaskClosable |
                   NSWindowStyleMaskMiniaturizable)
        backing:NSBackingStoreBuffered
        defer:NO];

    [_window setTitle:@"NightBuild License Agreement"];
    [_window setDelegate:self];
    [_window center];

    NSView *content = [_window contentView];
    CGFloat margin = 22.0;
    CGFloat buttonHeight = 34.0;
    CGFloat scrollHeight = frame.size.height - 3 * margin - buttonHeight;

    NSRect scrollFrame = NSMakeRect(
        margin, margin + buttonHeight,
        frame.size.width - 2 * margin, scrollHeight);

    _scrollView = [[NSScrollView alloc] initWithFrame:scrollFrame];
    [_scrollView setHasVerticalScroller:YES];
    [_scrollView setHasHorizontalScroller:NO];
    [_scrollView setBorderType:NSBezelBorder];
    [_scrollView setAutoresizingMask:
        NSViewWidthSizable | NSViewHeightSizable];

    NSString *text = documentText();
    NSFont *font = [NSFont userFixedPitchFontOfSize:12.0];
    NSDictionary *attributes = @{NSFontAttributeName: font};

    NSRect measured = [text
        boundingRectWithSize:NSMakeSize(scrollFrame.size.width - 36.0,
                                        CGFLOAT_MAX)
        options:NSStringDrawingUsesLineFragmentOrigin
        attributes:attributes];

    CGFloat documentHeight =
        MAX(ceil(measured.size.height) + 40.0, scrollHeight + 200.0);

    _textView = [[NSTextView alloc]
        initWithFrame:NSMakeRect(
            0, 0, scrollFrame.size.width - 18.0, documentHeight)];

    [_textView setString:text];
    [_textView setFont:font];
    [_textView setEditable:NO];
    [_textView setSelectable:YES];
    [_textView setRichText:NO];
    [_textView setHorizontallyResizable:NO];
    [_textView setVerticallyResizable:NO];
    [_textView setTextContainerInset:NSMakeSize(12.0, 12.0)];

    [_scrollView setDocumentView:_textView];
    [content addSubview:_scrollView];

    _acceptButton = [[NSButton alloc]
        initWithFrame:NSMakeRect(
            frame.size.width - margin - 124.0,
            margin, 124.0, buttonHeight)];

    [_acceptButton setTitle:@"Accept"];
    [_acceptButton setBezelStyle:NSBezelStyleRounded];
    [_acceptButton setTarget:self];
    [_acceptButton setAction:@selector(acceptLicense:)];
    [_acceptButton setEnabled:NO];
    [_acceptButton setAutoresizingMask:
        NSViewMinXMargin | NSViewMaxYMargin];
    [content addSubview:_acceptButton];

    NSButton *decline = [[NSButton alloc]
        initWithFrame:NSMakeRect(margin, margin, 124.0, buttonHeight)];

    [decline setTitle:@"Decline"];
    [decline setBezelStyle:NSBezelStyleRounded];
    [decline setTarget:self];
    [decline setAction:@selector(declineLicense:)];
    [decline setAutoresizingMask:NSViewMaxXMargin | NSViewMaxYMargin];
    [content addSubview:decline];

    [[_scrollView contentView] setPostsBoundsChangedNotifications:YES];
    [[NSNotificationCenter defaultCenter]
        addObserver:self
        selector:@selector(scrollPositionChanged:)
        name:NSViewBoundsDidChangeNotification
        object:[_scrollView contentView]];

    [_window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (void)scrollPositionChanged:(NSNotification *)notification {
    (void)notification;

    NSClipView *clip = [_scrollView contentView];
    NSRect clipBounds = [clip bounds];
    NSRect documentFrame = [_textView frame];

    if (NSMaxY(clipBounds) >= NSMaxY(documentFrame) - 3.0)
        [_acceptButton setEnabled:YES];
}

- (void)acceptLicense:(id)sender {
    (void)sender;

    if (![_acceptButton isEnabled])
        return;

    if (!writeMarker()) {
        NSAlert *alert = [[NSAlert alloc] init];
        [alert setMessageText:@"Could not save license acceptance"];
        [alert setInformativeText:
            @"NightBuild could not write the acceptance marker in "
             "~/Library/Application Support/NightBuild/."];
        [alert addButtonWithTitle:@"OK"];
        [alert runModal];
        [alert release];
        return;
    }

    _accepted = YES;
    [NSApp stop:nil];
    [_window orderOut:nil];
}

- (void)declineLicense:(id)sender {
    (void)sender;
    _accepted = NO;
    [NSApp stop:nil];
    [_window orderOut:nil];
}

- (void)windowWillClose:(NSNotification *)notification {
    (void)notification;
    [NSApp stop:nil];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:
    (NSApplication *)sender {
    (void)sender;
    return YES;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [_window release];
    [_scrollView release];
    [_textView release];
    [_acceptButton release];
    [super dealloc];
}

@end

extern "C" int nightbuild_license_is_accepted(void) {
    return access(markerPath().fileSystemRepresentation, F_OK) == 0 ? 1 : 0;
}

extern "C" int nightbuild_show_license_window(void) {
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];

        NBLicenseDelegate *delegate = [[NBLicenseDelegate alloc] init];
        [app setDelegate:delegate];
        [app run];

        int accepted = delegate.accepted ? 1 : 0;
        [app setDelegate:nil];
        [delegate release];

        return accepted;
    }
}
