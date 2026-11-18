# Run from tools/ui: python gen_ui.py (needs rsvg-convert). Copy out/*.png to compat/gd2208-geode5/resources.
# NexLevel UI textures: drawn as SVG, rendered at 4x (Geode "uhd"). Sizes below are in points.
import subprocess, os
S = 4  # px per point
OUT = 'out'; SVG = 'svg'
os.makedirs(OUT, exist_ok=True); os.makedirs(SVG, exist_ok=True)

def render(name, w, h, body, defs=''):
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{w*S}" height="{h*S}" viewBox="0 0 {w} {h}">
<defs>{defs}</defs>{body}</svg>'''
    p = f'{SVG}/{name}.svg'
    open(p, 'w').write(svg)
    subprocess.run(['rsvg-convert', p, '-o', f'{OUT}/{name}.png'], check=True)

def rrect(x, y, w, h, r, **a):
    attrs = ' '.join(f'{k.replace("_","-")}="{v}"' for k, v in a.items())
    return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" ry="{r}" {attrs}/>'

# ---- panel: main popup (radius 16, violet glow margin 10) ----
def panel(name, r, glow, size, fillTop, fillBot, alpha, borderA, glowA):
    m = glow; w = h = size
    defs = f'''<linearGradient id="f" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="{fillTop}"/><stop offset="1" stop-color="{fillBot}"/></linearGradient>
<linearGradient id="b" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#a070ff"/><stop offset=".5" stop-color="#7b3cff"/><stop offset="1" stop-color="#c43cff"/></linearGradient>
<linearGradient id="hl" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#ffffff" stop-opacity=".10"/><stop offset=".25" stop-color="#ffffff" stop-opacity="0"/></linearGradient>
<filter id="g" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="{glow*0.45}"/></filter>'''
    body = ''
    if glow:
        body += rrect(m, m + 1.5, w - 2*m, h - 2*m, r, fill='#000000', opacity=.55, filter='url(#g)')
        body += rrect(m, m, w - 2*m, h - 2*m, r, fill='none', stroke='#8b3dff', stroke_width=3, opacity=glowA, filter='url(#g)')
    body += rrect(m, m, w - 2*m, h - 2*m, r, fill='url(#f)', fill_opacity=alpha)
    body += rrect(m + .6, m + .6, w - 2*m - 1.2, h - 2*m - 1.2, r - .6, fill='url(#hl)')
    body += rrect(m + .5, m + .5, w - 2*m - 1, h - 2*m - 1, r - .5, fill='none', stroke='url(#b)', stroke_width=1, stroke_opacity=borderA)
    render(name, w, h, body, defs)

panel('panel', 16, 10, 60, '#1b1840', '#0b0a1a', .97, 1, .55)
panel('toast', 8, 6, 36, '#1a1636', '#0e0c20', .96, 1, .45)
panel('card', 10, 0, 28, '#262b56', '#1a1d3e', .88, .35, 0)

# ---- input field ----
defs = '''<linearGradient id="f" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#07080f"/><stop offset="1" stop-color="#0e1022"/></linearGradient>'''
render('field', 24, 24, rrect(0, 0, 24, 24, 8, fill='url(#f)') +
       rrect(.5, .5, 23, 23, 7.5, fill='none', stroke='#9a6bff', stroke_width=1, stroke_opacity=.55), defs)

# ---- pills (buttons): 64x32 pt, radius 16 ----
def pill(name, c1, c2, alpha=1, border=None, borderA=.5, gloss=True, shadow=True):
    w, h = 72, 40; m = 4; ph = h - 2*m; r = ph/2
    defs = f'''<linearGradient id="f" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="{c1}"/><stop offset="1" stop-color="{c2}"/></linearGradient>
<linearGradient id="gl" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#ffffff" stop-opacity=".32"/><stop offset=".5" stop-color="#ffffff" stop-opacity=".04"/><stop offset="1" stop-color="#000000" stop-opacity=".12"/></linearGradient>
<filter id="s" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="1.4"/></filter>'''
    body = ''
    if shadow: body += rrect(m, m + 1.6, w - 2*m, ph, r, fill='#000000', opacity=.45, filter='url(#s)')
    body += rrect(m, m, w - 2*m, ph, r, fill='url(#f)', fill_opacity=alpha)
    if gloss: body += rrect(m, m, w - 2*m, ph, r, fill='url(#gl)')
    if border: body += rrect(m + .4, m + .4, w - 2*m - .8, ph - .8, r - .4, fill='none', stroke=border, stroke_width=.8, stroke_opacity=borderA)
    else: body += rrect(m + .4, m + .4, w - 2*m - .8, ph - .8, r - .4, fill='none', stroke='#ffffff', stroke_width=.6, stroke_opacity=.30)
    render(name, w, h, body, defs)

pill('btn_primary', '#7b3cff', '#c43cff')
pill('btn_secondary', '#262b55', '#1d2146', alpha=.95, border='#9a6bff', borderA=.6, gloss=True)
pill('btn_danger', '#ff4468', '#b8309a')
pill('btn_ghost', '#000000', '#000000', alpha=0, border='#aab0d8', borderA=.45, gloss=False, shadow=False)
pill('btn_good', '#16c79a', '#0f9d8c')

# ---- buttons: slanted like the NexLevel logo (replaces the round pills above) ----
# Option A: slanted like the logo (parallelogram), 72x40 texture, shape 64x32, skew 7
def slant(name, fill, stroke, strokeA=1, glow=False, fillA=1):
    w,h,m,k=72,40,4,7
    pts=f'{m+k},{m} {w-m},{m} {w-m-k},{h-m} {m},{h-m}'
    defs=f'''<linearGradient id="f" x1="0" y1="0" x2="1" y2="1">{fill}</linearGradient>
<linearGradient id="gl" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fff" stop-opacity=".22"/><stop offset=".5" stop-color="#fff" stop-opacity="0"/></linearGradient>
<filter id="b" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="1.6"/></filter>'''
    body=''
    if glow: body+=f'<polygon points="{pts}" fill="none" stroke="{stroke}" stroke-width="2.2" opacity=".6" filter="url(#b)"/>'
    body+=f'<polygon points="{pts}" fill="url(#f)" fill-opacity="{fillA}" stroke-linejoin="round"/>'
    body+=f'<polygon points="{pts}" fill="url(#gl)"/>'
    body+=f'<polygon points="{pts}" fill="none" stroke="{stroke}" stroke-width="1.1" stroke-opacity="{strokeA}" stroke-linejoin="round"/>'
    render(name,w,h,body,defs)
slant('btn_primary','<stop offset="0" stop-color="#7b3cff"/><stop offset="1" stop-color="#c43cff"/>','#e0c8ff',.9,True)
slant('btn_secondary','<stop offset="0" stop-color="#0d0b18"/><stop offset="1" stop-color="#0d0b18"/>','#9b4dff',1,True)
slant('btn_danger','<stop offset="0" stop-color="#ff3d6e"/><stop offset="1" stop-color="#b8309a"/>','#ffc0d0',.8,True)
slant('btn_ghost','<stop offset="0" stop-color="#000"/><stop offset="1" stop-color="#000"/>','#9aa0c8',.6,False,0)

# ---- icons: white on transparent, 16x16 pt, tinted in game ----
I = {
 'room': '<path d="M3 8.2 8 4l5 4.2V13a.8.8 0 0 1-.8.8H9.6V10H6.4v3.8H3.8A.8.8 0 0 1 3 13z" fill="#fff"/>',
 'players': '<circle cx="6" cy="5.6" r="2.3" fill="#fff"/><path d="M1.8 13c0-2.5 1.9-4 4.2-4s4.2 1.5 4.2 4z" fill="#fff"/><circle cx="11.2" cy="6.2" r="1.8" fill="#fff" opacity=".7"/><path d="M10.6 9.3c1.9-.2 3.7 1 3.7 3.7h-3" fill="#fff" opacity=".7"/>',
 'chat': '<path d="M2.5 4.2c0-.9.7-1.6 1.6-1.6h7.8c.9 0 1.6.7 1.6 1.6v5c0 .9-.7 1.6-1.6 1.6H7l-3 2.6v-2.6h-.1c-.8 0-1.4-.7-1.4-1.6z" fill="#fff"/>',
 'history': '<circle cx="8" cy="8" r="5.4" fill="none" stroke="#fff" stroke-width="1.6"/><path d="M8 5v3.3l2.2 1.4" fill="none" stroke="#fff" stroke-width="1.6" stroke-linecap="round"/>',
 'network': '<rect x="2.5" y="10" width="2.4" height="3.5" rx=".6" fill="#fff"/><rect x="6.8" y="7" width="2.4" height="6.5" rx=".6" fill="#fff"/><rect x="11.1" y="3.5" width="2.4" height="10" rx=".6" fill="#fff"/>',
 'settings': '<path d="M8 1.8l1.2 1.6 1.9-.5.4 1.9 1.8.8-.6 1.9 1.1 1.6-1.6 1.2.2 2-1.9.3-.9 1.7-1.8-.8-1.8.8-.9-1.7-1.9-.3.2-2L1.8 9.1l1.1-1.6-.6-1.9 1.8-.8.4-1.9 1.9.5z" fill="#fff"/><circle cx="8" cy="8.2" r="2.2" fill="#000"/>',
 'lock': '<rect x="3.5" y="7" width="9" height="7" rx="1.5" fill="#fff"/><path d="M5.3 7V5.3a2.7 2.7 0 0 1 5.4 0V7" fill="none" stroke="#fff" stroke-width="1.6"/>',
 'warning': '<path d="M8 1.8 15 14H1z" fill="#fff" stroke="#fff" stroke-width="1" stroke-linejoin="round"/><rect x="7.2" y="5.6" width="1.6" height="4.6" rx=".8" fill="#000"/><circle cx="8" cy="12" r=".95" fill="#000"/>',
 'crown': '<path d="M2 5l3 3 3-4.5L11 8l3-3-1.3 7.5H3.3z" fill="#fff"/>',
 'check': '<path d="M3 8.4l3.2 3.2L13 4.8" fill="none" stroke="#fff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>',
 'info': '<circle cx="8" cy="8" r="6" fill="#fff"/><rect x="7.2" y="7" width="1.6" height="4.6" rx=".8" fill="#000"/><circle cx="8" cy="4.9" r="1" fill="#000"/>',
 'error': '<circle cx="8" cy="8" r="6" fill="#fff"/><path d="M5.8 5.8l4.4 4.4M10.2 5.8l-4.4 4.4" stroke="#000" stroke-width="1.6" stroke-linecap="round"/>',
 'close': '<path d="M4 4l8 8M12 4l-8 8" stroke="#fff" stroke-width="1.8" stroke-linecap="round"/>',
 'pin': '<path d="M8 1.8a4.4 4.4 0 0 1 4.4 4.4c0 3.2-4.4 8-4.4 8s-4.4-4.8-4.4-8A4.4 4.4 0 0 1 8 1.8z" fill="#fff"/><circle cx="8" cy="6.2" r="1.6" fill="#000"/>',
 'eye': '<path d="M1.5 8S4 3.8 8 3.8 14.5 8 14.5 8 12 12.2 8 12.2 1.5 8 1.5 8z" fill="#fff"/><circle cx="8" cy="8" r="2.2" fill="#000"/>',
 'copy': '<rect x="5.2" y="5.2" width="8" height="8.6" rx="1.4" fill="#fff"/><rect x="2.6" y="2.4" width="8" height="8.6" rx="1.4" fill="none" stroke="#fff" stroke-width="1.3" opacity=".7"/>',
 'refresh': '<path d="M12.8 6.6A5 5 0 1 0 13 9.2" fill="none" stroke="#fff" stroke-width="1.7" stroke-linecap="round"/><path d="M13.6 2.6v4.4H9.2z" fill="#fff"/>',
 'globe': '<circle cx="8" cy="8" r="5.8" fill="none" stroke="#fff" stroke-width="1.4"/><ellipse cx="8" cy="8" rx="2.5" ry="5.8" fill="none" stroke="#fff" stroke-width="1.2"/><path d="M2.4 8h11.2" stroke="#fff" stroke-width="1.2"/>',
}
for n, b in I.items():
    render('icon_' + n, 16, 16, b)
print(sorted(os.listdir(OUT)))
