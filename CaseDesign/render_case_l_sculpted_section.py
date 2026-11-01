"""Dimensioned section through actual exported CAD at Y=-12 mm."""
from pathlib import Path
import json
import cadquery as cq
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.collections import PolyCollection
R=Path(__file__).resolve().parent/'prototype-l-sculpted'
report=json.loads((R/'validation.json').read_text())
slab=cq.Workplane('XY').box(8,.02,1.6,centered=False).translate((12.5,-12.01,5.85)).val()
fig,ax=plt.subplots(figsize=(12,4.3),dpi=160)
fig.patch.set_facecolor('#f5f4f0');ax.set_facecolor('#f5f4f0')
for n,col in report['colors'].items():
    if not n.startswith('lid'):continue
    s=cq.importers.importStep(str(R/(n+'.step'))).val().intersect(slab)
    if not s.Solids():continue
    vertices,triangles=s.tessellate(.005)
    polygons=[[(vertices[i].x,vertices[i].z) for i in t] for t in triangles]
    ax.add_collection(PolyCollection(polygons,facecolors=[col],edgecolors='none',antialiased=False))
# Actual section geometry establishes these datums; annotations show nominal dimensions.
ax.annotate('',xy=(17.08,7.25),xytext=(17.08,6.85),arrowprops={'arrowstyle':'<->','color':'#c96517','lw':1.5})
ax.text(17.3,7.07,'0.40 mm relief',color='#a64b0c',fontsize=10,va='center')
ax.annotate('',xy=(14.1,6.65),xytext=(14.1,6.05),arrowprops={'arrowstyle':'<->','color':'white','lw':1.2})
ax.text(14.3,6.32,'0.60 mm carrier web',color='white',fontsize=10,va='center')
ax.text(14.2,7.12,'GREY PANEL',fontsize=9,color='#353b3d',ha='center')
ax.text(19.2,7.12,'GREY PANEL',fontsize=9,color='#353b3d',ha='center')
ax.text(12.6,7.58,'ACTUAL CAD SECTION  /  Y = −12 mm',fontsize=13,weight='bold',color='#273034')
ax.text(12.6,7.42,'Panel tops share L’s original height; bevelled edges descend into a supported joint.',fontsize=10,color='#556065')
ax.set_xlim(12.5,20.5);ax.set_ylim(5.95,7.75)
ax.set_xlabel('X / mm',color='#556065');ax.set_ylabel('Z / mm',color='#556065')
ax.set_yticks([6.05,6.65,6.85,7.25]);ax.tick_params(labelsize=9,colors='#556065')
for side in ['top','right']:ax.spines[side].set_visible(False)
for side in ['bottom','left']:ax.spines[side].set_color('#b6bdbd')
fig.tight_layout();fig.savefig(R/'surface-section.png',facecolor=fig.get_facecolor())
print('Wrote surface-section.png')
