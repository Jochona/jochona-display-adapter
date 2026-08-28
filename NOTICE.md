# Notices and third-party provenance

Jochona Display Adapter is a derivative of
[VirtualDrivers/Virtual-Display-Driver](https://github.com/VirtualDrivers/Virtual-Display-Driver)
(MIT licensed), which is itself a derivative of Microsoft's IndirectDisplay
sample driver from
[microsoft/Windows-driver-samples](https://github.com/microsoft/Windows-driver-samples)
(Microsoft Public License, "MS-PL"). This file preserves both upstream
notices in full, plus a summary of what in this repository derives from
which upstream project.

This repository is licensed **MIT** (see [`LICENSE`](LICENSE)) for
Jochona-authored code, **AND MS-PL** for the portions carried forward
from Microsoft's IndirectDisplay sample (the IddCx driver-callback
architecture in `driver/JochonaDisplayAdapter/Driver.{h,cpp}`, adapted
through `legacy/upstream-vdd`). Per MS-PL §3(C)/(D), this notice and the
full MS-PL text below travel with any redistribution of that portion of
the source.

## Provenance map

| This repository | Derived from | License |
|---|---|---|
| `legacy/upstream-vdd/` | `VirtualDrivers/Virtual-Display-Driver` (full pre-fork history preserved via `git log --follow`) | MIT |
| `driver/JochonaDisplayAdapter/Driver.{h,cpp}` (IddCx adapter/monitor/swap-chain callback shapes, `Direct3DDevice`, `SwapChainProcessor`) | `legacy/upstream-vdd/Virtual Display Driver (HDR)/MttVDD/Driver.{h,cpp}`, itself derived from `microsoft/Windows-driver-samples/video/IndirectDisplay` (`IddSampleDriver`) | MIT AND MS-PL |
| `driver/JochonaDisplayAdapter/Trace.h` | `legacy/upstream-vdd/.../MttVDD/Trace.h` (WPP tracing pattern), itself from `IddSampleDriver` | MIT AND MS-PL |
| `Common/`, `ThirdParty/` (Windows-Driver-Frameworks submodule) | Unchanged from upstream | MIT (Common), MIT (Windows-Driver-Frameworks, Microsoft) |
| `include/jochona/`, `src/protocol/`, `tests/`, `driver/JochonaDisplayAdapter/{IoControl,Acl}.{h,cpp}`, `docs/` | Jochona-original, protocol v1.0 | MIT |
| `tests/third_party/doctest.h` | [doctest](https://github.com/doctest/doctest) v2.4.11 | MIT |

## Upstream credits (from Virtual-Display-Driver's README)

- [@ye4241](https://github.com/ye4241) — submitted the package to Microsoft (WinGet)
- [MikeTheTech](https://github.com/itsmikethetech) — Project Manager, Owner, and Programmer
- [zjoasan](https://github.com/zjoasan) — scripts, EDID integration, installer
- [Bud](https://github.com/bud3699) — former Lead Programmer
- [Roshkins](https://github.com/roshkins/IddSampleDriver) — original IddSampleDriver fork
- [Baloukj](https://github.com/baloukj/IddSampleDriver) — 8-bit/10-bit support
- [Anakngtokwa](https://github.com/Anakngtokwa) — driver source research
- [Microsoft](https://github.com/microsoft/Windows-driver-samples/tree/master/video/IndirectDisplay) — IndirectDisplay sample driver (original driver code)
- [AKATrevorJay](https://github.com/akatrevorjay/edid-generator) — hi-res EDID reference
- [LexTrack](https://github.com/lextrack/) — MiniScreenRecorder script

## MIT License (Virtual Display Driver)

```
MIT License

Copyright (c) 2024 Virtual Display

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Microsoft Public License (MS-PL) — Windows-driver-samples

```
The Microsoft Public License (MS-PL)
Copyright (c) 2015 Microsoft

This license governs use of the accompanying software. If you use the software, you
 accept this license. If you do not accept the license, do not use the software.

1. Definitions
 The terms "reproduce," "reproduction," "derivative works," and "distribution" have the
 same meaning here as under U.S. copyright law.
 A "contribution" is the original software, or any additions or changes to the software.
 A "contributor" is any person that distributes its contribution under this license.
 "Licensed patents" are a contributor's patent claims that read directly on its contribution.

2. Grant of Rights
 (A) Copyright Grant- Subject to the terms of this license, including the license conditions and limitations in section 3, each contributor grants you a non-exclusive, worldwide, royalty-free copyright license to reproduce its contribution, prepare derivative works of its contribution, and distribute its contribution or any derivative works that you create.
 (B) Patent Grant- Subject to the terms of this license, including the license conditions and limitations in section 3, each contributor grants you a non-exclusive, worldwide, royalty-free license under its licensed patents to make, have made, use, sell, offer for sale, import, and/or otherwise dispose of its contribution in the software or derivative works of the contribution in the software.

3. Conditions and Limitations
 (A) No Trademark License- This license does not grant you rights to use any contributors' name, logo, or trademarks.
 (B) If you bring a patent claim against any contributor over patents that you claim are infringed by the software, your patent license from such contributor to the software ends automatically.
 (C) If you distribute any portion of the software, you must retain all copyright, patent, trademark, and attribution notices that are present in the software.
 (D) If you distribute any portion of the software in source code form, you may do so only under this license by including a complete copy of this license with your distribution. If you distribute any portion of the software in compiled or object code form, you may only do so under a license that complies with this license.
 (E) The software is licensed "as-is." You bear the risk of using it. The contributors give no express warranties, guarantees or conditions. You may have additional consumer rights under your local laws which this license cannot change. To the extent permitted under your local laws, the contributors exclude the implied warranties of merchantability, fitness for a particular purpose and non-infringement.
```

## doctest (MIT)

`tests/third_party/doctest.h` is vendored unmodified from
[doctest/doctest](https://github.com/doctest/doctest), Copyright (c)
2016-2023 Viktor Kirilov, MIT License. See the header's own comment block
for the full license text.
