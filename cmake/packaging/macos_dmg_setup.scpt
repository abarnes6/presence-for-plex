-- AppleScript for DMG window customization
tell application "Finder"
    tell disk "Presence For Plex"
        open
        set current view of container window to icon view
        set toolbar visible of container window to false
        set statusbar visible of container window to false
        set the bounds of container window to {400, 100, 900, 500}
        set viewOptions to the icon view options of container window
        set arrangement of viewOptions to not arranged
        set icon size of viewOptions to 72
        set background picture of viewOptions to file ".background:background.png"
        set position of item "Presence For Plex.app" of container window to {125, 180}
        set position of item "Applications" of container window to {375, 180}
        close
        open
        update without registering applications
        delay 2
    end tell
end tell
