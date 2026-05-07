#!/usr/bin/env python3
"""
generate_tab_icons.py  — v2
Generates 8 animated GIFs for OrcaSlicer top-bar tab buttons.
Size: 24x24px (fits inside the 40x36 em-scaled Button with room for label)
BG:  transparent → composited onto #2D2D30 (tab bar dark bg) for palette

Chosen animations:
  3d          — segment wave flash
  auxiliary   — rows light up top→bottom
  calibration — full crosshair hunts/drifts
  home        — house grows and shrinks (pulse)
  monitor     — print head + nozzle travel left↔right
  multi       — two windows swap blue/gold focus
  presets     — knobs slide in opposite directions
  preview     — layer planes fan out upward (coloured)
"""
import math, os
from PIL import Image, ImageDraw

OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tab_icon_gifs")
os.makedirs(OUT_DIR, exist_ok=True)

SIZE    = 24      # px — slightly larger than SVG 20px for clarity at toolbar size
N       = 24      # frames
DUR     = 60      # ms/frame  ≈16 fps
BG_COMP = (45, 45, 48)   # #2D2D30 tab bar bg

# colours matching the interactive preview
C_BLUE   = ( 89, 179, 248)
C_GREEN  = (160, 224,  96)
C_AMBER  = (248, 184,  89)
C_PURPLE = (232, 110, 232)

def new_frame():
    return Image.new("RGBA", (SIZE, SIZE), (0,0,0,0))

def save_gif(frames, name, duration=DUR):
    path = os.path.join(OUT_DIR, f"tab_{name}.gif")
    out = []
    for f in frames:
        bg = Image.new("RGB", (SIZE, SIZE), BG_COMP)
        bg.paste(f, mask=f.split()[3])
        out.append(bg.quantize(colors=64, dither=0))
    out[0].save(path, save_all=True, append_images=out[1:],
                loop=0, duration=duration, disposal=2)
    print(f"  {path}  ({os.path.getsize(path):,} bytes)")

def ease(t): return t*t*(3-2*t)
def lerp(a,b,t): return a+(b-a)*t
def L(d,x1,y1,x2,y2,col,w=1): d.line([(x1,y1),(x2,y2)],fill=col,width=w)

# Scale factor: our SIZE vs SVG 20px viewbox
S = SIZE / 20.0
def sv(x): return x * S   # scale a SVG coordinate to pixel

# ── 3D view — segment wave flash ─────────────────────────────────────────────
def make_3d():
    # Box corners in SVG coords, scaled to px
    TL=(sv(1.5),sv(6.5)); TR=(sv(17.5),sv(6.5)); TOP=(sv(9.7),sv(2.6))
    MID=(sv(9.7),sv(10.4)); BL=(sv(1.5),sv(14.5)); BR=(sv(17.5),sv(14.5))
    segs = [
        [TL,TOP], [TOP,TR],          # top two roof edges
        [TL,MID],[TR,MID],            # top-face diagonals
        [TL,BL], [TR,BR],[BR,BL],    # sides + bottom
    ]
    NSEG = len(segs)
    frames=[]
    for i in range(N):
        f=new_frame(); d=ImageDraw.Draw(f)
        t=i/N
        for si,(p1,p2) in enumerate(segs):
            phase=(t - si/NSEG)%1.0
            bri = 0.15+0.85*max(0.0,math.sin(phase*math.pi*2)) if phase<0.5 else 0.15
            bri = max(0.15,min(1.0,bri))
            L(d,p1[0],p1[1],p2[0],p2[1],(*C_BLUE,int(bri*255)),w=1)
        frames.append(f)
    save_gif(frames,"3d")

# ── Auxiliary — rows light up top→bottom ──────────────────────────────────────
def make_auxiliary():
    rows=[sv(5.5),sv(8.5),sv(11.5),sv(14.5)]
    frames=[]
    for i in range(N):
        f=new_frame(); d=ImageDraw.Draw(f)
        t=i/N
        # outer box dim
        d.rounded_rectangle([sv(1.5),sv(2.5),sv(17.5),sv(17.5)],radius=1,outline=(*C_BLUE,60),width=1)
        for ri,ry in enumerate(rows):
            phase=(t-ri/len(rows))%1.0
            if phase<0.1: bri=ease(phase/0.1)
            elif phase<0.2: bri=1.0
            elif phase<0.3: bri=ease(1-(phase-0.2)/0.1)
            else: bri=0.2
            bri=max(0.2,min(1.0,bri))
            a=int(bri*255); w=max(1,round(bri*2))
            L(d,sv(4.5),ry,sv(5.5),ry,(*C_BLUE,a),w=w)
            L(d,sv(8.5),ry,sv(14.5),ry,(*C_BLUE,a),w=w)
        frames.append(f)
    save_gif(frames,"auxiliary")

# ── Calibration — crosshair hunts ────────────────────────────────────────────
def make_calibration():
    hunt=[(0,0),(sv(2),-sv(2)),(-sv(1.5),sv(2)),(sv(1.5),sv(1.5)),(-sv(2),-sv(1))]
    NH=len(hunt)
    def cross(d,ox,oy,a=255):
        c=(*C_GREEN,a)
        # corner brackets (approximate the SVG paths)
        L(d,sv(7.5)+ox,sv(17.5)+oy,sv(1.5)+ox,sv(17.5)+oy,c)
        L(d,sv(1.5)+ox,sv(17.5)+oy,sv(1.5)+ox,sv(11.5)+oy,c)
        L(d,sv(17.5)+ox,sv(11.5)+oy,sv(17.5)+ox,sv(17.5)+oy,c)
        L(d,sv(17.5)+ox,sv(17.5)+oy,sv(11.5)+ox,sv(17.5)+oy,c)
        L(d,sv(11.5)+ox,sv(2.5)+oy,sv(17.5)+ox,sv(2.5)+oy,c)
        L(d,sv(17.5)+ox,sv(2.5)+oy,sv(17.5)+ox,sv(7.5)+oy,c)
        L(d,sv(1.5)+ox,sv(7.5)+oy,sv(1.5)+ox,sv(2.5)+oy,c)
        L(d,sv(1.5)+ox,sv(2.5)+oy,sv(7.5)+ox,sv(2.5)+oy,c)
        # crosshair arms
        L(d,sv(9.5)+ox,sv(2.5)+oy,sv(9.5)+ox,sv(4.5)+oy,c)
        L(d,sv(9.5)+ox,sv(7.5)+oy,sv(9.5)+ox,sv(11.5)+oy,c)
        L(d,sv(9.5)+ox,sv(14.5)+oy,sv(9.5)+ox,sv(17.5)+oy,c)
        L(d,sv(17.5)+ox,sv(9.5)+oy,sv(14.5)+ox,sv(9.5)+oy,c)
        L(d,sv(4.5)+ox,sv(9.5)+oy,sv(1.5)+ox,sv(9.5)+oy,c)
        L(d,sv(11.5)+ox,sv(9.5)+oy,sv(7.5)+ox,sv(9.5)+oy,c)
    frames=[]
    for i in range(N):
        f=new_frame(); d=ImageDraw.Draw(f)
        t=i/N; seg=t*NH
        i0=int(seg)%NH; i1=(i0+1)%NH; lt=ease(seg-int(seg))
        ox=lerp(hunt[i0][0],hunt[i1][0],lt)
        oy=lerp(hunt[i0][1],hunt[i1][1],lt)
        cross(d,ox,oy)
        frames.append(f)
    save_gif(frames,"calibration")

# ── Home — pulse grow/shrink ──────────────────────────────────────────────────
def make_home():
    cx,cy=sv(9.5),sv(10.0)
    # House outline as polygon points in SVG coords
    raw=[(1.5,7.5),(9.5,2.5),(17.5,7.5),(17.5,17.5),
         (12.5,17.5),(12.5,11.5),(7.5,11.5),(7.5,17.5),(1.5,17.5)]
    frames=[]
    for i in range(N):
        f=new_frame(); d=ImageDraw.Draw(f)
        t=i/N
        scale=1.0+0.16*ease(abs(math.sin(t*math.pi)))
        pts=[(cx+(sv(x)-cx)*scale, cy+(sv(y)-cy)*scale) for x,y in raw]
        d.polygon(pts,outline=(*C_AMBER,255),fill=None)
        frames.append(f)
    save_gif(frames,"home")

# ── Monitor — head+nozzle travel ─────────────────────────────────────────────
def make_monitor():
    C=C_PURPLE
    def static(d):
        d.rounded_rectangle([sv(1.5),sv(2.5),sv(17.5),sv(17.5)],radius=1,outline=(*C,255),width=1)
        L(d,sv(4.5),sv(14.5),sv(14.5),sv(14.5),(*C,255))  # bed
        L(d,sv(3.0),sv(6.5),sv(16.0),sv(6.5),(*C,255))    # rail
    def head(d,hx):
        x1,y1,x2,y2=hx-sv(2),sv(4.5),hx+sv(2),sv(8.0)
        d.rectangle([x1,y1,x2,y2],outline=(*C,255),width=1)
        d.rectangle([hx-sv(0.5),sv(6.5),hx+sv(0.5),sv(7.5)],fill=(*C,255))
        ty=sv(11.0)
        L(d,hx-sv(1),sv(9.5),hx,ty,(*C,255)); L(d,hx+sv(1),sv(9.5),hx,ty,(*C,255))
    frames=[]
    for i in range(N):
        f=new_frame(); d=ImageDraw.Draw(f); t=i/N
        hx=sv(5.5)+sv(9.0)*(0.5+0.5*math.sin(t*2*math.pi-math.pi/2))
        static(d); head(d,hx)
        frames.append(f)
    save_gif(frames,"monitor")

# ── Multi — two windows swap focus ────────────────────────────────────────────
def make_multi():
    def win(d,x1,y1,x2,y2,col):
        d.rounded_rectangle([x1,y1,x2,y2],radius=1,outline=col,width=1)
        w=x2-x1; ry=y1+(y2-y1)*0.35
        L(d,x1+sv(3),ry,x2-sv(3),ry,col)          # rail
        L(d,x1+sv(3),y2-sv(3),x2-sv(3),y2-sv(3),col) # bed
        hx1=x1+w*0.35; hx2=hx1+w*0.25
        hy1=y1+sv(2); hy2=hy1+(y2-y1)*0.28
        d.rectangle([hx1,hy1,hx2,hy2],outline=col,width=1)
        mx=(hx1+hx2)/2; ny=hy2+sv(2.5)
        L(d,mx-sv(1),hy2+sv(0.5),mx,ny,col); L(d,mx+sv(1),hy2+sv(0.5),mx,ny,col)
    frames=[]
    for i in range(N):
        f=new_frame(); d=ImageDraw.Draw(f); t=i/N
        if t<0.5:
            ft=ease(t*2); fa=int((1-0.8*ft)*255); ba=int((0.2+0.8*ft)*255)
        else:
            ft=ease((t-0.5)*2); fa=int((0.2+0.8*ft)*255); ba=int((1-0.8*ft)*255)
        win(d,sv(5.5),sv(2.5),sv(17.5),sv(13.5),(*C_AMBER,ba))  # back gold
        win(d,sv(1.5),sv(6.5),sv(13.5),sv(17.5),(*C_BLUE, fa))  # front blue
        frames.append(f)
    save_gif(frames,"multi")

# ── Presets — knobs slide opposite directions ──────────────────────────────────
def make_presets():
    C=C_AMBER
    K1R=sv(5.5); K2R=sv(13.5); TRAVEL=sv(5.0)
    def knob(d,kx,ky,col):
        x1,y1,x2,y2=kx-sv(2),ky-sv(3),kx+sv(2),ky+sv(3)
        d.rectangle([x1,y1,x2,y2],fill=(*BG_COMP,255))
        d.rounded_rectangle([x1,y1,x2,y2],radius=1,outline=col,width=1)
    frames=[]
    for i in range(N):
        f=new_frame(); d=ImageDraw.Draw(f); t=i/N
        L(d,sv(1.5),sv(5.5),sv(17.5),sv(5.5),(*C,180))
        L(d,sv(1.5),sv(14.5),sv(17.5),sv(14.5),(*C,180))
        k1x=K1R+TRAVEL*math.sin(t*2*math.pi)
        k2x=K2R-TRAVEL*math.sin(t*2*math.pi)
        knob(d,k1x,sv(5.5),(*C,255)); knob(d,k2x,sv(14.5),(*C,255))
        frames.append(f)
    save_gif(frames,"presets")

# ── Preview — layers fan out upward in staggered sequence ────────────────────
def make_preview():
    cols=[C_AMBER,C_GREEN,C_BLUE]
    base_y=[sv(6.4),sv(10.4),sv(15.0)]
    FAN=sv(4.0); PH=0.18
    def layer(d,cy,col):
        lx,rx,ax=sv(1.5),sv(17.5),sv(9.7); ady=sv(3.9)
        L(d,lx,cy,ax,cy-ady,col); L(d,ax,cy-ady,rx,cy,col)
    frames=[]
    for i in range(N):
        f=new_frame(); d=ImageDraw.Draw(f); t=i/N
        for li in [2,1,0]:
            phase=(t-li*PH)%1.0
            fan_t=ease(abs(math.sin(phase*math.pi)))
            cy=base_y[li]-FAN*fan_t
            layer(d,cy,(*cols[li],255))
        frames.append(f)
    save_gif(frames,"preview")

if __name__=="__main__":
    print(f"Generating {SIZE}×{SIZE} animated GIFs → {OUT_DIR}/")
    make_3d(); make_auxiliary(); make_calibration(); make_home()
    make_monitor(); make_multi(); make_presets(); make_preview()
    print("Done. ✓")
