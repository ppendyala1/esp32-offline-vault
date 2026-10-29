"""CadQuery 2.7; millimetres. Prototype with adjustable adhesive board mounts."""
import cadquery as cq
from pathlib import Path
import json
ROOT=Path(__file__).resolve().parents[1]
def box(x,y,z,at): return cq.Workplane('XY').box(x,y,z,centered=(True,True,False)).translate(at)
def cyl(r,h,at): return cq.Workplane('XY').circle(r).extrude(h).translate(at)
W,L,H,T=90,130,36,2.4
base=box(W,L,H,(0,0,0)).edges('|Z').fillet(5)
base=base.cut(box(W-2*T,L-2*T,H,(0,0,T)).edges('|Z').fillet(3))
posts=[(-38,-58),(38,-58),(-38,58),(38,58)]
for x,y in posts:
 base=base.union(cyl(4.5,H-T,(x,y,T))).cut(cyl(1.3,12,(x,y,H-12)))
# Oversize USB access accommodates board alignment; USB end of Feather faces +Y.
base=base.cut(box(20,8,12,(-20,64,7)))
# Battery bay: interior 38 x 67, removable foam tape retention, no compression.
for x in (1,41): base=base.union(box(2,69,8,(x,15,T)))
for y in (-19.5,49.5): base=base.union(box(38,2,8,(21,y,T)))
# Wiring notch at battery lead end.
base=base.cut(box(15,5,6,(21,49.5,6)))
lid=box(W,L,T,(0,0,H)).edges('|Z').fillet(5)
# Display window generously clears active area; position PCB using foam tape on bezel.
lid=lid.cut(box(27,16,8,(0,27,H-2)))
for x,y in posts: lid=lid.cut(cyl(1.7,8,(x,y,H-2)))
for x,y in [(-16,-15),(16,-15),(-16,-43),(16,-43)]:
 lid=lid.cut(cyl(8.2,8,(x,y,H-2)))
# 6 mm panel toggle mounting hole on side, above battery bay-free region.
base=base.cut(cq.Workplane('YZ').circle(3.2).extrude(10).translate((-48,-42,20)))
# Separate insulation tray for RTC/Feather; tape to base left of battery.
tray=box(30,83,1.6,(-22,15,T))
parts={'base':base,'lid':lid,'electronics_tray':tray}
assembly=cq.Assembly()
report={}
for name,obj in parts.items():
 s=obj.val(); assert s.isValid() and len(obj.solids().vals())==1
 cq.exporters.export(obj,str(ROOT/'step'/f'{name}.step'))
 printable=obj.translate((0,0,-s.BoundingBox().zmin))
 cq.exporters.export(printable,str(ROOT/'stl'/f'{name}.stl'),tolerance=0.08,angularTolerance=0.1)
 assembly.add(obj,name=name)
 report[name]={'valid':True,'volume_mm3':round(s.Volume(),2),'solids':len(obj.solids().vals())}
assembly.save(str(ROOT/'step'/'enclosure_assembly.step'))
assert base.intersect(lid).val().Volume()<0.001
(ROOT/'validation.json').write_text(json.dumps(report,indent=2))
print(report)
