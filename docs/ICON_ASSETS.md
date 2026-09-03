# Product icon source assets

The high-resolution PNG files below are the retained source artwork. They are
versioned in Git and must not be replaced by an ICO extraction, screenshot, or
other downscaled derivative.

| Product | Source artwork | Derived Windows icon |
| --- | --- | --- |
| Windows Fader Bridge for EUCON | `src/FaderBridge.EuconHost/assets/WindowsFaderBridge.png` | `src/FaderBridge.EuconHost/assets/WindowsFaderBridge.ico` |
| Windows Fader Bridge for Mackie Control | `src/FaderBridge.MackieHost/assets/mackie-icon-source.png` | `src/FaderBridge.MackieHost/assets/WindowsFaderBridge.Mackie.ico` |
| UAD Console Bridge for EUCON | `src/ApolloBridge.EuconHost/assets/uad-console-bridge-source.png` | `src/ApolloBridge.EuconHost/assets/UADConsoleBridge.Eucon.ico` |

All three source images are 1254 × 1254 pixels. Application UI code should load
an appropriately sized frame from the derived multi-resolution ICO and must not
stretch the default 32 × 32 system icon for larger in-app artwork.
