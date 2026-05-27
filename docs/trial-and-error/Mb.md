# stoermelder MB

MB is a module for experimental replacement for Rack's module browser, formerly available in stoermelder's PackTau. It brings back the browser from Rack v0.6x and has a modified browsers from Rack v1.x and v2.x with adjustable preview size, favorites, extended filtering options, custom tags and more.

![MB intro](./Mb-intro.png)

### Introduction by Omri Cohen

<a href="https://www.youtube.com/embed/7DSTPIHWOVg?start=1640" target="_blank"><img src="https://img.youtube.com/vi/7DSTPIHWOVg/0.jpg" style="width:100%" /></a>

## Custom tags

MB maintains its own custom tag system separate from Rack's built-in tags. Custom tags are stored in your local Rack directory and can be exported/shared like favorites and hidden module settings.

**Adding custom tags** — Use the auto-generate functions described below, or add tags manually through the context menu of individual modules in the browser.

**Viewing and managing tags** — The context menu on the module lists all existing custom tags with options to delete them.

### Auto-generate custom tags

MB can automatically assign custom tags to modules using keyword matching. The context menu offers three auto-tagging options:

**Auto-generate custom tags** — Uses a curated rule set with ~70 tag categories covering synthesis techniques (Wavetable, FM Synthesis, Phase Modulation), filter types (Ladder Filter, Comb Filter), modulation utilities (Attenuverter, Comparator, Shift Register), effects (Bitcrusher, Tape, Spring Reverb), and more. Keywords are matched via fuzzy substring search against module names and descriptions.

**Auto-generate 'MetaModule' tag** — Connects to https://metamodule.info to download a list of MetaModule-compatible plugins and assigns the "MetaModule" tag to matching modules.

**Auto-generate tag from search** — Enter a custom search term (which becomes the tag name) and modules matching that query are tagged accordingly. For example, searching "Sequencer" would show all untagged modules containing "sequencer" in name/description.

All three options show a confirmation dialog listing the proposed tag assignments, allowing you to verify or adjust individual assignments before applying.

## Predefined tags

MB allows you to add or remove predefined tags (the classic VCV Rack tag aliases like "Attenuator", "Mixer", "MIDI", etc.) on individual modules. This is useful if a module has incorrect or incomplete tags. Please note, that modification on predefined tags are only visible within the module browsers of the MB module.

## *v2_mod* keyboard shortcuts

The *v2-mod* browser variant supports keyboard navigation and shortcuts:

**Navigation** (when search field is focused or in the module grid):
| Key | Action |
|-----|--------|
| *Click* | Add module | 
| `Shift`+*Click* | Add module, keep browser open |
| `↓` | Move down in the module grid |
| `↑` | Move up in the module grid |
| `→` | Move to the next module in the row |
| `←` | Move to the previous module in the row |
| `Enter` | Add the selected module to the rack |
| `Escape` | Close the browser |
| `Backspace` | Clear search and filters (when search is empty) |
| `Space` | Toggle Favorites filter (when search is empty) |
| `Ctrl/Cmd`+`Space` | Toggle listing of hidden modules
| `Ctrl/Cmd`+`1` | Open Brand filter dropdown |
| `Ctrl/Cmd`+`2` | Open Tag filter dropdown |
| `Ctrl/Cmd`+`3` | Open Custom Tag filter dropdown |

**Module hover shortcuts** (hover over a module):
| Key | Action |
|-----|--------|
| `Ctrl/Cmd`+`F` | Toggle favorite status of the hovered module |
| `Ctrl/Cmd`+`H` | Toggle hidden status of the hovered module |

**Dropdown menus** (Brand, Tag, Custom Tag filters):
| Key | Action |
|-----|--------|
| `↓`/`↑` | Navigate up/down through items |
| `←`/`→` | Navigate to previous/next row |
| *Any key* | Filter items by typing (incremental filter) |
| `Backspace` | Clear filter text (show all items) |
| `Enter` | Toggle selection of the highlighted item |

## Tips

- Display of hidden modules can be toggled by hotkey `Shift`+`Space`.

- Hitting the `Space`-key will toggle the _Favorites_ category.

- Favorites and hidden modules are stored in your local Rack-directory. You can share them or copy them to another computer by MB's export/import function on the context menu.

- For _v1 mod_ and _v2 mod_, by context menu option the "brands"-section can be hidden (added in v1.9.0).

- For _v1 mod_ and _v2 mod_, by context memu option also the modules' description can be searched (added in v1.9.0).

- **Search threshold** — Controls how fuzzy the fuzzy search matching is. Lower values (default 0.5) show more results with looser matching, while higher values (1.0) require closer matches.

- **Favorite modes** — MB supports two favorite modes (VCV Rack / MB) controlling how favorites are stored and displayed.

- **Magnifier overlay** — When enabled, hovering over a module preview in the browser shows a zoomed magnification loupe following the cursor. 

## Changelog

- v1.8
    - Initial release
- v1.9
    - Added option to hide the "brands" section of the V1-browser (#223)
    - Added option to search module descriptions (https://github.com/stoermelder/vcvrack-packtau/pull/9)
- v2.0.0
    - Fixed usage in multiple plugin-instances
    - Fixed crash on exiting Rack after adding MB (#352)
    - Fixed wrong hotkey modifier on Mac (Ctrl instead of Cmd) on Space-key
    - Added missing template loading after adding a module (#369)
- v2.4.0
    - Added custom tags for user-defined module grouping
        - with auto-tagging using a curated list of tags
        - with auto-tagging for available modules on MetaModule
    - Added "v2 mod" browser variant
        - with improved drop-down menus, also with keyboard-filtering
        - with keyboard-navigation using cursor-keys
        - with hotkeys Ctrl/Cmd+1/2/3 for Brand/Tags/Custom Tag drop-down menus
        - with keyboard-navigation and filtering within the drop-down menus
    - Added ability to add/remove predefined tags (only within MB)
    - Added fuzzy search (similar to the default module browser)
        - with setting for the fuzzy search threshold
    - Added an option to select "Favorite" handling (Legacy (MB) / Built-in VCV Rack)
    - Added an option to highlight favorites
    - Added model magnifier loupe option
    - Added integration into Rack's menu bar
    - Added Shift+Click to add a module without closing the browser
    - Added option to apply VCV Library whitelisting
    - Added option to show deprecated module models (now hidden by default) (#440)
    - Changed hotkey to toggle hidden modules to Shift+Space (because of Spotlight on Mac)
    - Fixed broken button of "Favorites" category