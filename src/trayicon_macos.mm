#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#include "trayicon.h"

// Objective-C class to handle menu callbacks
@interface TrayIconDelegate : NSObject
@property (nonatomic, assign) TrayIcon* trayIconPtr;
- (void)menuExitClicked:(id)sender;
@end

@implementation TrayIconDelegate
- (void)menuExitClicked:(id)sender {
    if (self.trayIconPtr) {
        LOG_DEBUG("TrayIconDelegate", "Exit menu item clicked");
        auto callback = self.trayIconPtr->getExitCallback();
        if (callback) {
            callback();
        } else {
            LOG_ERROR("TrayIconDelegate", "Exit callback is null");
        }
    } else {
        LOG_ERROR("TrayIconDelegate", "TrayIcon pointer is null");
    }
}
@end

// C++ implementation using Objective-C
TrayIcon::TrayIcon(const std::string &appName) : initialized(false), exitCallback(nullptr), tooltip(appName)
{
    LOG_DEBUG("TrayIcon", "Creating tray icon for macOS");
    
    // Ensure NSApplication is initialized and activated
    [NSApplication sharedApplication];
    
    // Explicitly activate the application as a UI element
    ProcessSerialNumber psn = {0, kCurrentProcess};
    TransformProcessType(&psn, kProcessTransformToUIElementApplication);
    
    // Set the activation policy to be an accessory app (menu bar app)
    [[NSApplication sharedApplication] setActivationPolicy:NSApplicationActivationPolicyAccessory];
    
    LOG_DEBUG("TrayIcon", "NSApplication initialized and activated");
    
    // Create the status item
    statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSSquareStatusItemLength];
    if (!statusItem) {
        LOG_ERROR("TrayIcon", "Failed to create NSStatusItem");
        return;
    }
    LOG_DEBUG("TrayIcon", "NSStatusItem created successfully");
    
    // Load icon from bundle resources
    NSImage *icon = nil;
    
    // Try to load from the application bundle
    NSBundle *bundle = [NSBundle mainBundle];
    if (bundle) {
        LOG_DEBUG_STREAM("TrayIcon", "Bundle path: " << [[bundle bundlePath] UTF8String]);
        
        // Look for icon.png in the Resources directory
        NSString *iconPath = [bundle pathForResource:@"icon" ofType:@"png"];
        if (iconPath) {
            LOG_DEBUG_STREAM("TrayIcon", "Found icon at path: " << [iconPath UTF8String]);
            icon = [[NSImage alloc] initWithContentsOfFile:iconPath];
            if (icon) {
                LOG_DEBUG("TrayIcon", "Successfully loaded icon from path");
            } else {
                LOG_ERROR("TrayIcon", "Failed to load icon from path");
            }
        } else {
            LOG_DEBUG("TrayIcon", "icon.png not found in resources, trying asset catalog");
            // Try to load from app's Resources/Assets.car (for asset catalogs)
            icon = [NSImage imageNamed:@"AppIcon"];
            if (icon) {
                LOG_DEBUG("TrayIcon", "Loaded icon from asset catalog");
            } else {
                LOG_DEBUG("TrayIcon", "AppIcon not found in asset catalog");
            }
        }
    } else {
        LOG_ERROR("TrayIcon", "Could not get main bundle");
    }
    
    // Fall back to system icon if app icon not found
    if (!icon) {
        LOG_WARNING("TrayIcon", "Could not load custom icon, using system icon instead");
        icon = [NSImage imageNamed:@"NSStatusAvailable"];
        if (icon) {
            LOG_DEBUG("TrayIcon", "Successfully loaded system icon NSStatusAvailable");
        } else {
            LOG_ERROR("TrayIcon", "Failed to load system icon NSStatusAvailable");
        }
    }
    
    if (icon) {
        // Set icon size appropriate for menu bar
        [icon setSize:NSMakeSize(18, 18)];
        LOG_DEBUG("TrayIcon", "Set icon size to 18x18");
        
        // Check if image is valid
        LOG_DEBUG_STREAM("TrayIcon", "Icon size: " << [icon size].width << "x" << [icon size].height);
        
        [statusItem setImage:icon];
        LOG_DEBUG("TrayIcon", "Set status item image");
        [statusItem setHighlightMode:YES];
        LOG_DEBUG("TrayIcon", "Enabled highlight mode");
    } else {
        LOG_ERROR("TrayIcon", "Failed to load any icon");
    }
    
    // Create the menu
    menu = [[NSMenu alloc] init];
    if (!menu) {
        LOG_ERROR("TrayIcon", "Failed to create NSMenu");
        return;
    }
    LOG_DEBUG("TrayIcon", "NSMenu created successfully");
    
    // Create the Quit menu item
    quitItem = [[NSMenuItem alloc] initWithTitle:@"Exit" 
                                          action:@selector(menuExitClicked:) 
                                   keyEquivalent:@"q"];
    
    // Create delegate to handle menu actions
    TrayIconDelegate *delegate = [[TrayIconDelegate alloc] init];
    delegate.trayIconPtr = this;
    [quitItem setTarget:delegate];
    LOG_DEBUG("TrayIcon", "Created exit menu item with delegate");
    
    [menu addItem:quitItem];
    [statusItem setMenu:menu];
    LOG_DEBUG("TrayIcon", "Attached menu to status item");
    
    // Set the tooltip
    [statusItem setToolTip:[NSString stringWithUTF8String:tooltip.c_str()]];
    LOG_DEBUG_STREAM("TrayIcon", "Set tooltip to: " << tooltip);
    
    initialized = true;
    LOG_INFO("TrayIcon", "macOS tray icon created successfully");
}

TrayIcon::~TrayIcon()
{
    if (initialized) {
        LOG_DEBUG("TrayIcon", "Destroying macOS tray icon");
        // The ARC (Automatic Reference Counting) will handle releasing these objects
        statusItem = nil;
        menu = nil;
        quitItem = nil;
    }
}

void TrayIcon::show()
{
    if (initialized) {
        LOG_DEBUG("TrayIcon", "Showing macOS tray icon");
        [statusItem setVisible:YES];
        
        // Force update of the status bar
        [[NSStatusBar systemStatusBar] setLength:NSStatusBar.systemStatusBar.thickness];
        
        // Additional debug to confirm visibility
        BOOL isVisible = [statusItem isVisible];
        LOG_DEBUG_STREAM("TrayIcon", "Status item visibility after show: " << (isVisible ? "visible" : "not visible"));
    } else {
        LOG_ERROR("TrayIcon", "Cannot show tray icon - not initialized");
    }
}

void TrayIcon::hide()
{
    if (initialized) {
        LOG_DEBUG("TrayIcon", "Hiding macOS tray icon");
        [statusItem setVisible:NO];
    } else {
        LOG_ERROR("TrayIcon", "Cannot hide tray icon - not initialized");
    }
}

void TrayIcon::setTooltip(const std::string &tip)
{
    tooltip = tip;
    if (initialized && statusItem) {
        LOG_DEBUG_STREAM("TrayIcon", "Setting macOS tooltip to: " << tip);
        [statusItem setToolTip:[NSString stringWithUTF8String:tip.c_str()]];
    } else if (!initialized) {
        LOG_ERROR("TrayIcon", "Cannot set tooltip - tray icon not initialized");
    } else {
        LOG_ERROR("TrayIcon", "Cannot set tooltip - status item is null");
    }
}

void TrayIcon::setExitCallback(std::function<void()> callback)
{
    LOG_DEBUG("TrayIcon", "Setting exit callback");
    exitCallback = callback;
}

std::function<void()> TrayIcon::getExitCallback() const
{
    return exitCallback;
}
#endif