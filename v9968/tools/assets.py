"""Compile cat/rabbit outline backgrounds, pastel title and 9x9 blue rocket."""
from pathlib import Path
import math,json,random,os
from PIL import Image,ImageDraw,ImageFont,ImageOps
R=Path(__file__).resolve().parents[1];O=R/'assets/compiled';O.mkdir(parents=True,exist_ok=True)
# Fixed gameplay palette: dark yellow cat outlines and dim white rabbit outlines.
pal=[(0,0,0),(3,4,7),(6,9,15),(10,15,24),(5,20,31),(0,31,27),(16,31,31),
     (31,31,31),(7,5,1),(24,8,31),(31,6,17),(8,31,3),(31,24,5),
     (23,31,12),(5,5,5),(2,10,4)]
rgb=[v*255//31 for c in pal for v in c]
def canvas(size):
 im=Image.new('P',size);im.putpalette(rgb*16);return im
def packed(im):
 b=im.tobytes();assert max(b)<16
 return bytes((b[i]<<4)|b[i+1] for i in range(0,len(b),2))
def quant(im):
 p=canvas((1,1));im=im.quantize(palette=p,dither=Image.Dither.NONE)
 im=im.point([i%16 for i in range(256)]);im.putpalette(rgb*16);return im
font='C:/Windows/Fonts/bahnschrift.ttf'
# Compile supplied/generated outline art to one-bit lines in the 4bpp screen.
# The stars are native procedural game decoration with rounded corners.
def star(draw,cx,cy,r):
 pts=[(cx+math.sin(i*math.pi/5)*r*(1 if i%2==0 else .46),cy-math.cos(i*math.pi/5)*r*(1 if i%2==0 else .46)) for i in range(10)]
 path=[]
 for i,p in enumerate(pts):
  prev=pts[(i-1)%10];nxt=pts[(i+1)%10]
  a=(p[0]*.72+prev[0]*.28,p[1]*.72+prev[1]*.28)
  b=(p[0]*.72+nxt[0]*.28,p[1]*.72+nxt[1]*.28)
  for j in range(5):
   t=j/4;path.append(((1-t)**2*a[0]+2*t*(1-t)*p[0]+t*t*b[0],(1-t)**2*a[1]+2*t*(1-t)*p[1]+t*t*b[1]))
 draw.line(path+[path[0]],fill=8,width=1)
backgrounds=[]
for name in ['cat','rabbit','pair']:
 bg=canvas((256,256))
 src=Image.open(R/('assets/face-'+name+'.png')).convert('L')
 ink=ImageOps.invert(src).point(lambda v:255 if v>100 else 0)
 bounds=ink.getbbox();ink=ink.crop(bounds)
 ink=ImageOps.contain(ink,(222,176),Image.Resampling.LANCZOS)
 mask=ink.point(lambda v:255 if v>85 else 0)
 position=((256-ink.width)//2,24+(180-ink.height)//2)
 bg.paste(14 if name=='rabbit' else 8,position,mask)
 if name=='pair':
  split=round(ink.width*.52)
  bg.paste(14,(position[0]+split,position[1]),mask.crop((split,0,ink.width,ink.height)))
 d=ImageDraw.Draw(bg)
 for x,y,rad in [(10,31,5),(244,48,4),(11,110,4),(245,131,5),(14,195,5),(240,200,4),(83,208,3),(184,21,3)]:star(d,x,y,rad)
 bg.save(O/('background-'+name+'.png'));(O/('background-'+name+'.bin')).write_bytes(packed(bg))
 backgrounds.append(bg)
bg=backgrounds[0];bg.save(O/'background.png');(O/'background.bin').write_bytes(packed(bg))
# Four colored bullet patterns share palette zero, followed by the player.
pat=canvas((256,16))
# Distinct silhouettes and two-tone rims, with a fixed bright centre.
# Each tile retains the same 7x7 nonzero backing for the fast bitmap path.
shapes=[
 ['00000000','000AAA00','00ABBBA0','0ABWWWBA','0ABWWWBA','0ABWWWBA','00ABBBA0','000AAA00'],
 ['00000000','0000A000','000ABA00','00ABWBA0','0ABWWWBA','00ABWBA0','000ABA00','0000A000'],
 ['00000000','0000A000','000ABA00','000BWB00','00ABWBA0','000BWB00','000ABA00','0000A000'],
 ['00000000','0000A000','00AABAA0','000BWB00','0ABWWWBA','000BWB00','00AABAA0','0000A000']]
# Gold spark is seven pixels across; its white core marks the bullet centre.
colors=[{'A':4,'B':6,'W':7},{'A':9,'B':10,'W':7},{'A':10,'B':13,'W':7},{'A':12,'B':11,'W':7}]
for group,shape in enumerate(shapes):
 assert len(shape)==8 and all(len(row)==8 for row in shape)
 for y in range(8):
  for x in range(8):
   v=colors[group].get(shape[y][x],1 if 1<=x<=7 and 1<=y<=7 else 0)
   for yy in range(2):
    for xx in range(2):pat.putpixel((group*16+x*2+xx,y*2+yy),v)
# Blue rocket: the same 9x9 visible bounds as the previous cross (x/y 4..12).
ship=canvas((16,16));sd=ImageDraw.Draw(ship)
sd.polygon([(8,4),(10,7),(10,9),(12,11),(12,12),(9,11),(7,11),(4,12),(4,11),(6,9),(6,7)],fill=4)
sd.line((8,5,8,10),fill=6,width=1)
sd.point((7,7),fill=2);sd.point((9,7),fill=2)
ship.putpixel((8,8),7)
ship.putpixel((8,12),12);pat.paste(ship,(64,0));ship.save(O/'player.png')
(O/'patterns.bin').write_bytes(packed(pat));pat.save(O/'patterns.png')
atlas=canvas((256,16))
for group in range(4):
 for y in range(8):
  for x in range(8):atlas.putpixel((group*16+x,y),pat.getpixel((group*16+x*2,y*2)))
digits=[31599,29850,29671,31207,18925,31183,31695,9383,31727,31215]
letters=[23530,15083,25166,15211,29391,4815,27470,23533,29847,11044,23277,29257,23549,24573,11114,4843,28522,23275,14478,9367,31597,11117,24557,23213,9389,29351]
def glyph(draw,bits,x,y):
 for row in range(5):
  for col in range(3):
   if bits&1:draw.rectangle((x+col*2,y+row*2,x+col*2+1,y+row*2+1),fill=7)
   bits>>=1
ad=ImageDraw.Draw(atlas)
for digit,bits in enumerate(digits):glyph(ad,bits,80+digit*8,0)
atlas.save(O/'bullets.png');(O/'atlas.bin').write_bytes(packed(atlas))
titlepal=[(31,29,26),(31,27,28),(25,23,29),(22,19,26),(19,26,30),(20,28,25),(31,31,29),(12,10,15),(27,20,13),(27,19,26),(29,18,21),(22,29,21),(31,26,16),(30,27,22),(23,23,25),(26,22,28)]
trgb=[v*255//31 for c in titlepal for v in c]
def tc(n):return tuple(trgb[n*3:n*3+3])
scale=4
panel_rgb=Image.new('RGB',(1024,672),tc(0));pd=ImageDraw.Draw(panel_rgb)
pd.rounded_rectangle((32,12,992,636),radius=76,fill=tc(6),outline=tc(1),width=12)
rounded=os.environ.get('TITLE_FONT','C:/Windows/Fonts/comicbd.ttf')
def titletext(text,y,size,color):
 f=ImageFont.truetype(rounded,size*scale);w=pd.textlength(text,font=f)
 pd.text(((1024-w)/2,y*scale),text,font=f,fill=tc(color),stroke_width=0)
titletext('SAToReinker',14,27,9);titletext('Over',46,23,3)
pd.rounded_rectangle((216,348,808,452),radius=48,fill=tc(1))
titletext('SPACE  START',88,12,7)
titletext('X  REPLAY',119,10,7);titletext('L  LOAD    S  SAVE',140,9,7)
tp=Image.new('P',(1,1));tp.putpalette(trgb*16)
panel=panel_rgb.resize((256,168),Image.Resampling.LANCZOS).quantize(palette=tp,dither=Image.Dither.NONE)
panel=panel.point([i%16 for i in range(256)]);panel.putpalette(trgb*16)
pd=ImageDraw.Draw(panel)
for x,y,radius in [(24,40,7),(234,62,6),(30,130,5),(223,132,5)]:star(pd,x,y,radius)
panel.save(O/'title.png');(O/'title.bin').write_bytes(packed(panel))
(O/'palette-title.json').write_text(json.dumps(titlepal))
(O/'palette.json').write_text(json.dumps(pal))
inc='static const u8 colors16[48]={'+','.join(str(v) for c in pal for v in c)+'};\n'
inc+='static const u8 colors16_title[48]={'+','.join(str(v) for c in titlepal for v in c)+'};\n'
inc+='static const int rotations[64][2]={'+','.join('{%d,%d}'%(round(math.cos(i*math.tau/64)*16384),round(math.sin(i*math.tau/64)*16384)) for i in range(64))+'};\n'
(R/'src/assets.inc').write_text(inc)
print('SCREEN 5: three 32768-byte face backgrounds, pastel title, 9x9 blue rocket')
