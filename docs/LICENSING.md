# Licensing

*This is an explanation of the project's choices, not legal advice.*

## The pieces

| Component | License | How it's used |
|---|---|---|
| Listen to the Broadcast (this repository) | GPL-3.0-or-later | All source in `source/`, `tests/` and `tools/` |
| [JUCE](https://github.com/juce-framework/JUCE) 8.0.15 | AGPLv3 (or a commercial JUCE licence) | Plugin framework, GUI and audio utilities; compiled into the plugin |
| Steinberg VST3 SDK (bundled with JUCE) | MIT (since VST3 SDK 3.8) | The VST3 plugin interface |

## Why GPLv3 and AGPLv3 can be combined

GPLv3 section 13 explicitly permits combining GPLv3 code with code under the GNU AGPLv3 into a single work. AGPLv3 section 13 grants the same permission in the other direction. Each part keeps its own license:
- this project's code remains GPLv3-or-later;
- JUCE remains AGPLv3;
- the combined work, the released `.vst3` binary, must satisfy the AGPLv3's terms for the JUCE part.

In practice:
- **Source availability.** Anyone who receives the plugin binary is entitled to the complete corresponding source. It's in this public repository, and every release is built by GitHub Actions from a tagged commit.
- **The AGPL network clause (section 13)** is about users interacting with a modified program *remotely over a network*. The plugin runs locally and only listens to broadcast traffic; it offers no service to remote users. If someone built a hosted service from a modified version, that clause would apply to them.

## If you want to redistribute or sell it

- **Redistributing unchanged or modified builds with source available:** fine under GPLv3 + AGPLv3. Keep the license texts and make the source for your version available.
- **Distributing a closed-source or proprietary product** built on this code: not allowed by the GPL. The JUCE part would also need a commercial JUCE licence instead of the AGPLv3; see [JUCE's licensing page](https://juce.com/legal/juce-8-licence/).

## VST trademark

VST is a trademark of Steinberg Media Technologies GmbH. The VST3 SDK's MIT license covers the code, not the right to use the VST logo.
