# ESP32 Offline Vault enclosure — prototype R1

SolidWorks-compatible mechanical package. Native SLDPRT/SLDASM files are NOT included: SolidWorks was unavailable. STEP solids were generated and geometry-checked with CadQuery. Physical fit, printing, and SolidWorks import have not been tested.

## Open in SolidWorks
See the [rendered geometry previews](../../docs/HARDWARE_PREVIEWS.md) for assembled, exploded, and individual-part views. These are renders of the existing STL files, not SolidWorks screenshots or a verified import. The prototype is intended for wired breakout modules; no custom KiCad PCB is included.

1. Extract this folder. File > Open > select step/enclosure_assembly.step. Choose assembly import if offered. Units are millimetres.
2. Run Import Diagnostics. Save the assembly as enclosure.SLDASM and save its component parts when prompted. Keep all native files together.
3. Alternatively open base.step, lid.step and electronics_tray.step separately and Save As SLDPRT. These are imported solids, not native sketch/extrude feature trees. Use direct editing or FeatureWorks, or change source/build.py and regenerate.
4. For a manual assembly, insert each part at the common origin and fix it there. All STEP parts already use assembled coordinates. Do not auto-center them separately.
5. Optional source/ImportParts.bas automates individual STEP imports and saves. Import it into a new SolidWorks VBA macro; set folder path. It is supplied untested. Manual import is the verified-format fallback.

## Files
- step/: three individual solid parts plus assembled STEP.
- stl/: each printable part moved to Z=0, millimetres.
- source/build.py: reproducible parametric CadQuery 2.7 model; pip install cadquery==2.7.0, then python source/build.py.
- PARTS.md: selected hardware, source links, assembly supplies.
- validation.json: CAD kernel validity and volumes, not physical validation.

## Design
90 x 130 x 38.4 mm assembled, excluding button heads. Base height 36; walls/floor/lid 2.4 mm. Four lid screws at X +/-38, Y +/-58. Blind 2.6 mm pilot bores in base; 3.4 mm lid clearance. Use four M3 x 10 pan-head screws, carefully pre-tap pilot holes; do not force screws into printed plastic. Pilot size may need adjustment for printer/material.

Display opening 27 x 16 at (0,27); button holes diameter 16.4 at (+/-16,-15) and (+/-16,-43). Button labels: top-left UP, top-right DOWN, bottom-left BACK, bottom-right OK. Add labels with label tape. Battery cavity is 38 x 67 mm, 8 mm retaining walls. USB opening on +Y wall at X=-20 is 20 x 12 mm, lower edge Z=7. Switch hole is 6.4 mm on -X wall at Y=-42, Z=20.

## Mounting and assembly
Print base flat, opening up; lid flat; tray flat. Start with PLA fit prototype, 0.2 mm layers, 4 perimeters, 25% infill. USB opening may require bridging/support. Deburr holes. No support should touch the battery during use.

The electronics tray sits left of the battery. Mount Feather lengthwise with USB toward +Y. Use nonconductive removable foam mounting tape and insulating spacers to align connector with opening; use soldered low-profile wiring rather than stacked headers. Place RTC below Feather on the tray. Tray itself attaches with removable foam tape. No guessed PCB mounting-hole patterns are used.

Mount TFT to underside of lid using removable foam tape on PCB edges, align active display with window before pressing tape down. Do not tape across LCD, connector, pads or components. Leave service loops so lid opens without pulling wiring. Panel buttons fasten using their supplied nuts. Insulate solder terminals with heat-shrink. Keep button terminals and wiring in lower half, clear of battery.

Battery uses a small removable adhesive pad under its broad face; bay is a locator, not a clamp. Never tighten a strap across the pouch or place screw tips above it. Verify lead exit and connector reach. Power switch connects Feather EN to GND (off); charging can remain enabled when logic is off. Use Feather's charger; no external TP4056.

## Required fit checks before final print
- Print lid and test one button hole first; measure button nut diameter and below-panel terminal depth. Reserve 30 mm below lid in button region. Adjust holes if needed.
- Check actual display active-area alignment, PCB components and tape thickness; opening is a design allowance, not a manufacturer mounting drawing.
- Test USB cable plug shell through opening with Feather held at chosen mounting height.
- Check switch bushing/thread length against 2.4 mm wall; do not buy an unspecified switch without checking fit.
- Verify battery including protection circuit and lead bend fits without pressure. Product dimensions can vary.
- Inspect wiring clearance while closing lid; check screw engagement and no contact with electronics.

This package completes a first mechanical prototype, not hardware qualification or firmware. Firmware remains in the original repository. Suggested GitHub location: hardware/enclosure/. Keep original firmware history intact.
