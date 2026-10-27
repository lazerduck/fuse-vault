"""Draw exact STEP cross-sections of the J D-pad and flange."""
from pathlib import Path
import cadquery as cq
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.collections import PolyCollection
import numpy as np
R=Path(__file__).resolve().parent/'prototype-j';cx,cy=58.77,-10.38
parts={n:cq.importers.importStep(str(R/(n+'.step'))).val() for n in ['lid','dpad']}
def box(x0,x1,y0,y1,z0,z1):return cq.Workplane('XY').box(x1-x0,y1-y0,z1-z0,centered=False).translate((x0,y0,z0)).val()
slab=box(cx-11,cx+11,cy-.015,cy+.015,0,10)
colors={'lid':'#71889d','dpad':'#e7a432'}
def section(s):return s.rotate((cx,cy,0),(cx,cy,1),-45).intersect(slab)
def draw(ax,s,color,alpha=1):
 if not s.Solids():return
 vs,ts=s.tessellate(.012,.08)
 verts=np.array([[v.x-cx,v.z] for v in vs]);tris=verts[np.asarray(ts)]
 # Side walls project to zero-area triangles; only cross-section faces matter.
 u=tris[:,1]-tris[:,0];v=tris[:,2]-tris[:,0]
 area=np.abs(u[:,0]*v[:,1]-u[:,1]*v[:,0])
 ax.add_collection(PolyCollection(tris[area>1e-8],facecolors=color,edgecolors='none',alpha=alpha))
fig,axs=plt.subplots(1,2,figsize=(15,7),gridspec_kw={'width_ratios':[1.45,1]},facecolor='#f5f7fa')
for ax in axs:
 ax.set_facecolor('#f5f7fa');ax.set_aspect('equal');ax.set_ylim(.9,8.8)
 if ax is axs[0]:
  ax.axhspan(0,1.6,color='#358b77');ax.axhline(1.6,color='#246c5b',lw=1)
 for n in ['lid','dpad']:
  shape=parts[n]
  if ax is axs[1] and n=='dpad':shape=shape.translate((0,0,-3))
  draw(ax,section(shape),colors[n])
 ax.set_yticks([]);ax.set_xticks([])
 for spine in ax.spines.values():spine.set_visible(False)
axs[0].set_xlim(-10.7,10.7);axs[1].set_xlim(-7.2,7.2);axs[1].set_ylim(-1.8,8.8)
axs[0].set_title('Diagonal section through the retaining flange',loc='left',fontsize=14,pad=20)
axs[1].set_title('Assembly: insert upward from underneath',loc='left',fontsize=14,pad=20)
def note(ax,text,xy,xytext,align='left'):
 ax.annotate(text,xy=xy,xytext=xytext,fontsize=10,ha=align,va='center',color='#233344',
 arrowprops={'arrowstyle':'-','color':'#536475','lw':1.2},bbox={'boxstyle':'round,pad=.3','fc':'#f5f7fa','ec':'none'})
note(axs[0],'Moving D-pad',(-1,7.7),(-9.8,8.3))
note(axs[0],'Thicker lid corners\nstop upward lift',(-4.7,5.7),(-10,4.5))
note(axs[0],'Rounded heel rests\non the PCB',(0,1.6),(4.3,2.3))
axs[0].text(-10.2,1.15,'PCB reference',color='white',fontsize=10)
note(axs[1],'Flange stays\nbelow the lid',(-4.5,2.0),(-6.5,.4))
axs[1].annotate('',xy=(0,6.2),xytext=(0,4.2),arrowprops={'arrowstyle':'->','color':'#a46412','lw':2})
axs[1].text(1.0,5.2,'Insert',fontsize=10,color='#8a5412')
fig.suptitle('How revision J locates and retains the D-pad',x=.05,ha='left',fontsize=21,fontweight='bold',color='#233344')
fig.text(.05,.12,'AMBER  moving D-pad     BLUE-GREY  fixed lid     GREEN  PCB reference plane',fontsize=11,color='#233344')
fig.text(.05,.075,'One-piece D-pad; no separate bearing or keeper. Opening clearance: 0.35 mm per side. Lift blocked by 0.40 mm.',fontsize=11,color='#233344')
fig.text(.05,.035,'Section from the exported STEP geometry. Switch contact posts lie outside this diagonal section. Physical fit remains untested.',fontsize=10,color='#536475')
fig.subplots_adjust(left=.04,right=.97,top=.84,bottom=.20,wspace=.15)
fig.savefig(R/'dpad-cutaway.png',dpi=180,facecolor=fig.get_facecolor())
fig.savefig(R/'dpad-cutaway.svg',facecolor=fig.get_facecolor())
print(R/'dpad-cutaway.png')
