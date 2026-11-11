### Things that need implementation:
- Multiple viewer windows
    And support to combine the windows together with a page/tab system.
    There would only be one toolbar, and it controls the viewer window is focused/clicked on.
- Animation controls (also to be used as slideshow/document controls such as PDF)
- Clipboard support
- Image Exporting options (and the replacement of the 'Save Image As PNG' button)
    This would be implemented as a 'Save Image As' button and the save file dialoge handles the rest via the file type selector
- Cropping controls for export
- Export support implementation most plugins
- Proper settings menu
- Plugins menu (Plugin name, developer copywrite, formats, enable/disable, folder path selection, etc..)
- Keybinds and keybinds settings
- Image tiling options and detection for images that normally support that or contain data for it
- Better info dialogue
- Better UI (ui library)
- More interpolation options

### Needs fixing:
- Recent files, clicking on a file in the list does nothing.
- Archive plugins, these sometimes wont load or will behave weirdly
- Input handling in the UI
    Especially the mouse on the toolbar, clicks are handled as falling edge which causes buttons to be press when dragging windows sometimes.
- Performance issues with the GIF plugin (or maybe animation in general).