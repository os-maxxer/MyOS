#!/usr/bin/env python3
"""Generate the map data for the Solis Crusader Kings III app.

The land outlines come from Natural Earth 1:50m land, clipped to the game's
map box, simplified and written as tenths of a degree. The provinces and the
realms of 1066 are declared below: each province is a seed point and every
land pixel belongs to its nearest seed, so the political map has no hand-cut
polygons and the whole world can be re-tuned by editing the tables.

    python3 tools/gen_ck3_map.py [output.h]

The default output is include/solis/apps/ck3_map.h. Seeds that fall in the
sea are snapped to the nearest shore before they are written.
"""
import json
import math
import os
import sys
import urllib.request

# Map box, degrees. Tenths of a degree are used in the generated C.
WEST, SOUTH, EAST, NORTH = -12.0, 2.0, 118.0, 62.0
MAP_W, MAP_H = 1040, 600
SIMPLIFY = 0.075          # degrees, Douglas-Peucker tolerance
MIN_AREA = 0.02           # square degrees, drop specks smaller than this

SOURCE = ("https://raw.githubusercontent.com/nvkelso/natural-earth-vector/"
          "master/geojson/ne_50m_land.geojson")

T_PLAINS, T_FOREST, T_HILLS, T_MARSH, T_DESERT, T_MOUNTAIN = range(6)

# faith codes: 0 christian, 1 sunni, 2 shia, 3 pagan, 4 hindu, 5 buddhist,
# 6 jewish, 7 taoist
REALMS = [
    dict(name="Kingdom of England", label="ENGLAND", ruler="Harold II",
         house="Godwin", color=0xB4524E, faith=0, aggr=60,
         at=(-1.6, 53.0), provs=[
             ("Wessex", -1.4, 51.0, T_PLAINS, 5),
             ("Mercia", -2.3, 52.4, T_FOREST, 4),
             ("East Anglia", 1.4, 52.6, T_PLAINS, 4),
             ("Northumbria", -1.6, 54.3, T_HILLS, 3)]),
    dict(name="Kingdom of Alba", label="SCOTLAND", ruler="Malcolm III",
         house="Dunkeld", color=0x7C6FA8, faith=0, aggr=45,
         at=(-3.6, 57.6), provs=[
             ("Alba", -3.7, 56.4, T_HILLS, 3),
             ("Moray", -4.3, 57.6, T_HILLS, 2)]),
    dict(name="High Kingdom of Ireland", label="IRELAND", ruler="Diarmait",
         house="Ua Briain", color=0x5E9B63, faith=0, aggr=30,
         at=(-8.2, 53.4), provs=[
             ("Leinster", -6.4, 53.2, T_FOREST, 3),
             ("Ulster", -6.9, 54.5, T_HILLS, 2)]),
    dict(name="Kingdom of France", label="FRANCE", ruler="Philip I",
         house="Capet", color=0x4C79B8, faith=0, aggr=50,
         at=(1.9, 46.6), provs=[
             ("Ile-de-France", 2.4, 48.8, T_PLAINS, 6),
             ("Normandy", 1.0, 49.3, T_PLAINS, 5),
             ("Anjou", -0.6, 47.6, T_FOREST, 4),
             ("Aquitaine", -0.5, 44.9, T_FOREST, 5),
             ("Toulouse", 1.5, 43.7, T_PLAINS, 4),
             ("Burgundy", 4.9, 47.2, T_HILLS, 4)]),
    dict(name="Holy Roman Empire", label="HOLY ROMAN EMPIRE",
         ruler="Henry IV", house="Salian", color=0xA9A6C0, faith=0, aggr=55,
         at=(9.4, 50.2), provs=[
             ("Lotharingia", 6.1, 50.7, T_FOREST, 5),
             ("Frisia", 5.2, 52.3, T_MARSH, 4),
             ("Saxony", 11.6, 52.3, T_FOREST, 5),
             ("Thuringia", 10.6, 51.0, T_FOREST, 3),
             ("Franconia", 9.9, 49.9, T_HILLS, 4),
             ("Swabia", 8.6, 47.5, T_HILLS, 4),
             ("Bavaria", 11.7, 48.2, T_HILLS, 5),
             ("Bohemia", 14.4, 50.0, T_FOREST, 4),
             ("Austria", 16.3, 48.2, T_HILLS, 4),
             ("Carinthia", 13.3, 46.9, T_MOUNTAIN, 3),
             ("Lombardy", 9.4, 45.5, T_PLAINS, 5),
             ("Tuscany", 11.2, 43.4, T_HILLS, 4)]),
    dict(name="Kingdom of Denmark", label="DENMARK", ruler="Sweyn II",
         house="Estridsen", color=0xC97C6E, faith=0, aggr=50,
         at=(9.6, 56.9), provs=[
             ("Jutland", 10.2, 56.3, T_PLAINS, 3),
             ("Zealand", 12.2, 55.5, T_PLAINS, 3)]),
    dict(name="Kingdom of Norway", label="NORWAY", ruler="Harald III",
         house="Hardrada", color=0x8C6FA8, faith=0, aggr=65,
         at=(7.0, 61.3), provs=[
             ("Vestland", 5.6, 60.6, T_MOUNTAIN, 2),
             ("Viken", 10.6, 59.9, T_FOREST, 3)]),
    dict(name="Kingdom of Sweden", label="SWEDEN", ruler="Stenkil",
         house="Stenkil", color=0x5A86B8, faith=3, aggr=40,
         at=(16.4, 60.6), provs=[
             ("Svealand", 17.5, 59.8, T_FOREST, 3),
             ("Gotaland", 13.6, 58.3, T_FOREST, 2)]),
    dict(name="Duchy of Poland", label="POLAND", ruler="Boleslaw II",
         house="Piast", color=0xB98E5A, faith=0, aggr=45,
         at=(19.4, 51.8), provs=[
             ("Greater Poland", 17.6, 52.4, T_PLAINS, 4),
             ("Masovia", 20.0, 52.6, T_FOREST, 3),
             ("Little Poland", 20.4, 50.1, T_HILLS, 3),
             ("Silesia", 17.0, 51.1, T_PLAINS, 3)]),
    dict(name="Kingdom of Hungary", label="HUNGARY", ruler="Solomon",
         house="Arpad", color=0x8FA85E, faith=0, aggr=50,
         at=(20.2, 47.2), provs=[
             ("Pannonia", 18.8, 47.7, T_PLAINS, 4),
             ("Transylvania", 23.6, 46.2, T_MOUNTAIN, 3)]),
    dict(name="Kingdom of Croatia", label="CROATIA", ruler="Peter Kresimir",
         house="Kresimirovic", color=0x7FA8A0, faith=0, aggr=30,
         at=(16.4, 44.6), provs=[
             ("Slavonia", 16.2, 45.7, T_FOREST, 3),
             ("Dalmatia", 16.7, 43.6, T_MOUNTAIN, 3)]),
    dict(name="Kievan Rus", label="RUTHENIA", ruler="Iziaslav I",
         house="Rurikid", color=0x7F9B6A, faith=0, aggr=45,
         at=(33.5, 54.6), provs=[
             ("Kiev", 30.4, 50.4, T_PLAINS, 5),
             ("Pereyaslavl", 32.3, 49.9, T_PLAINS, 3),
             ("Novgorod", 31.4, 58.4, T_FOREST, 4),
             ("Polotsk", 28.7, 55.4, T_FOREST, 3),
             ("Galich", 24.8, 49.0, T_FOREST, 3),
             ("Suzdal", 40.3, 56.2, T_FOREST, 3)]),
    dict(name="Cuman-Kipchak Horde", label="CUMANIA", ruler="Sharukan",
         house="Kipchak", color=0xC0A878, faith=3, aggr=55,
         at=(47.0, 49.6), provs=[
             ("Don", 39.7, 47.3, T_PLAINS, 2),
             ("Volga Steppe", 45.6, 48.6, T_PLAINS, 2),
             ("Azov", 37.3, 46.6, T_PLAINS, 2),
             ("Crimea", 34.2, 45.2, T_PLAINS, 3)]),
    dict(name="Volga Bulgaria", label="VOLGA BULGARIA", ruler="Azfar",
         house="Bulgar", color=0x8C7FA8, faith=1, aggr=35,
         at=(50.4, 56.3), provs=[
             ("Bolghar", 49.3, 54.9, T_FOREST, 3),
             ("Bilyar", 50.6, 54.7, T_FOREST, 2)]),
    dict(name="Permian Tribes", label="PERM", ruler="Kudym",
         house="Perm", color=0x6FA88C, faith=3, aggr=25,
         at=(55.6, 59.6), provs=[
             ("Perm", 56.3, 58.4, T_FOREST, 2),
             ("Vyatka", 50.2, 58.1, T_FOREST, 2)]),
    dict(name="Byzantine Empire", label="BYZANTINE EMPIRE",
         ruler="Constantine X", house="Doukas", color=0x9B6FB0, faith=0,
         aggr=50, at=(27.4, 39.4), provs=[
             ("Constantinople", 28.8, 41.2, T_PLAINS, 6),
             ("Nicaea", 30.3, 40.2, T_HILLS, 4),
             ("Macedonia", 22.9, 40.7, T_HILLS, 4),
             ("Hellas", 23.5, 38.2, T_MOUNTAIN, 3),
             ("Bulgaria", 25.4, 43.0, T_HILLS, 3),
             ("Serbia", 20.9, 43.2, T_MOUNTAIN, 3)]),
    dict(name="Kingdom of Georgia", label="GEORGIA", ruler="Bagrat IV",
         house="Bagrationi", color=0xB07F5A, faith=0, aggr=35,
         at=(43.6, 42.4), provs=[
             ("Tbilisi", 44.7, 41.8, T_MOUNTAIN, 3),
             ("Armenia", 43.5, 40.4, T_MOUNTAIN, 3)]),
    dict(name="Great Seljuk Empire", label="SELJUK", ruler="Alp Arslan",
         house="Seljuk", color=0xA8C05E, faith=1, aggr=65,
         at=(55.0, 33.4), provs=[
             ("Isfahan", 51.7, 32.7, T_DESERT, 5),
             ("Baghdad", 44.4, 33.4, T_DESERT, 5),
             ("Rayy", 51.4, 35.6, T_DESERT, 4),
             ("Fars", 52.4, 29.7, T_DESERT, 3),
             ("Kerman", 57.1, 30.3, T_DESERT, 2),
             ("Khorasan", 62.1, 37.4, T_DESERT, 3),
             ("Azerbaijan", 46.3, 39.0, T_HILLS, 3)]),
    dict(name="Fatimid Caliphate", label="FATIMID", ruler="al-Mustansir",
         house="Fatimid", color=0x5FB08C, faith=2, aggr=45,
         at=(32.4, 26.4), provs=[
             ("Cairo", 31.2, 30.1, T_DESERT, 6),
             ("Palestine", 35.0, 31.7, T_DESERT, 4),
             ("Damascus", 36.3, 33.4, T_DESERT, 4),
             ("Cyrenaica", 20.1, 32.0, T_DESERT, 2)]),
    dict(name="Almoravid Emirate", label="ALMORAVID", ruler="Abu Bakr",
         house="Lamtuna", color=0xC87F6A, faith=1, aggr=60,
         at=(-6.4, 32.6), provs=[
             ("Marrakesh", 8.0, 31.7, T_DESERT, 4),
             ("Fez", -5.0, 34.1, T_HILLS, 4),
             ("Andalusia", -5.8, 37.5, T_PLAINS, 5),
             ("Tlemcen", -1.4, 34.9, T_HILLS, 3),
             ("Sijilmasa", -4.5, 31.3, T_DESERT, 2)]),
    dict(name="Ghana Empire", label="GHANA", ruler="Basi",
         house="Cisse", color=0xB8A85C, faith=3, aggr=30,
         at=(-8.6, 17.6), provs=[
             ("Koumbi", -7.9, 15.9, T_DESERT, 4),
             ("Mema", -4.7, 14.2, T_DESERT, 2)]),
    dict(name="Kanem Empire", label="KANEM", ruler="Arku",
         house="Sefuwa", color=0x7FA87F, faith=3, aggr=30,
         at=(15.4, 15.6), provs=[
             ("Njimi", 15.2, 13.2, T_DESERT, 3)]),
    dict(name="Kingdom of Makuria", label="NUBIA", ruler="Georgios",
         house="Makar", color=0xA88C6A, faith=0, aggr=25,
         at=(31.2, 20.4), provs=[
             ("Makuria", 30.6, 18.3, T_DESERT, 3),
             ("Alwa", 32.6, 15.5, T_DESERT, 2)]),
    dict(name="Sharifate of Mecca", label="ARABIA", ruler="Abu Hashim",
         house="Hashim", color=0xC0A05E, faith=1, aggr=30,
         at=(44.0, 19.6), provs=[
             ("Hejaz", 39.8, 21.5, T_DESERT, 3),
             ("Yemen", 44.2, 15.5, T_DESERT, 3)]),
    dict(name="Ghaznavid Empire", label="GHAZNAVID", ruler="Ibrahim",
         house="Ghaznavid", color=0xB08C7F, faith=1, aggr=50,
         at=(69.6, 31.4), provs=[
             ("Ghazni", 68.3, 33.6, T_MOUNTAIN, 4),
             ("Kabul", 69.3, 34.6, T_MOUNTAIN, 2),
             ("Lahore", 74.2, 31.6, T_PLAINS, 3)]),
    dict(name="Chola Empire", label="CHOLA", ruler="Virarajendra",
         house="Chola", color=0xB86F7F, faith=4, aggr=45,
         at=(79.0, 13.0), provs=[
             ("Tanjore", 79.2, 10.8, T_PLAINS, 4),
             ("Madurai", 78.0, 9.7, T_PLAINS, 2)]),
    dict(name="Western Chalukya", label="CHALUKYA", ruler="Someshvara II",
         house="Chalukya", color=0xC89F6F, faith=4, aggr=40,
         at=(75.4, 16.4), provs=[
             ("Kalyani", 76.8, 17.9, T_PLAINS, 3),
             ("Konkan", 73.4, 18.5, T_PLAINS, 3)]),
    dict(name="Pala Empire", label="PALA", ruler="Vigrahapala III",
         house="Pala", color=0x7F9B8C, faith=5, aggr=35,
         at=(86.6, 27.2), provs=[
             ("Pataliputra", 85.2, 25.6, T_PLAINS, 4),
             ("Gauda", 88.2, 24.6, T_MARSH, 3)]),
    dict(name="Great Liao", label="GREAT LIAO", ruler="Daozong",
         house="Yelu", color=0x6F8C7F, faith=5, aggr=50,
         at=(110.0, 46.6), provs=[
             ("Shangjing", 116.6, 43.8, T_PLAINS, 3),
             ("Orkhon", 102.9, 47.2, T_PLAINS, 1)]),
    dict(name="Qocho Uyghurs", label="QOCHO", ruler="Bilge",
         house="Yaglakar", color=0xA88CC0, faith=5, aggr=25,
         at=(88.4, 44.6), provs=[
             ("Beshbalik", 89.0, 44.2, T_DESERT, 3),
             ("Turfan", 89.3, 42.8, T_DESERT, 2)]),
    dict(name="Tibetan Kingdom", label="TIBET", ruler="Tshal",
         house="Sakya", color=0x8C9B7F, faith=5, aggr=25,
         at=(88.6, 31.6), provs=[
             ("Lhasa", 91.1, 29.8, T_MOUNTAIN, 3),
             ("Tsang", 88.7, 29.4, T_MOUNTAIN, 2)]),
    dict(name="Qarakhanid Khanate", label="KARAKHANID", ruler="Shams",
         house="Qarakhan", color=0xA0B07F, faith=1, aggr=40,
         at=(70.6, 41.0), provs=[
             ("Samarkand", 66.9, 39.7, T_DESERT, 4),
             ("Bukhara", 64.4, 39.8, T_DESERT, 4),
             ("Kashgar", 76.0, 39.6, T_DESERT, 3)]),
    dict(name="Khwarezm", label="KHWAREZM", ruler="Shah Malik",
         house="Anushtegin", color=0xC0B07F, faith=1, aggr=35,
         at=(58.4, 43.0), provs=[
             ("Urgench", 59.2, 41.6, T_DESERT, 3)]),
    dict(name="Emirate of Sicily", label="SICILY", ruler="Ayyub",
         house="Kalbids", color=0xB07FA8, faith=1, aggr=35,
         at=(14.4, 36.2), provs=[
             ("Palermo", 13.6, 37.9, T_PLAINS, 4)]),
    dict(name="Papal States", label="PAPAL STATES", ruler="Alexander II",
         house="Papacy", color=0xD8D2C0, faith=0, aggr=20,
         at=(12.1, 43.4), provs=[
             ("Rome", 12.5, 41.9, T_HILLS, 3)]),
    dict(name="Kingdom of Castile", label="CASTILE", ruler="Sancho II",
         house="Jimenez", color=0xC8A24F, faith=0, aggr=45,
         at=(-4.6, 40.0), provs=[
             ("Leon", -5.6, 42.6, T_HILLS, 3),
             ("Castile", -3.7, 41.7, T_PLAINS, 3)]),
    dict(name="Kingdom of Aragon", label="ARAGON", ruler="Sancho Ramirez",
         house="Jimenez", color=0xC97B4F, faith=0, aggr=40,
         at=(0.4, 42.9), provs=[
             ("Aragon", -0.9, 41.7, T_MOUNTAIN, 3),
             ("Barcelona", 2.2, 41.4, T_PLAINS, 4)]),
]


def fetch(path):
    cache = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         "..", ".cache", "ne_50m_land.geojson")
    cache = os.path.normpath(cache)
    if os.path.exists(cache):
        with open(cache, "r", encoding="utf-8") as f:
            return json.load(f)
    print("downloading", SOURCE)
    with urllib.request.urlopen(SOURCE, timeout=60) as r:
        data = json.loads(r.read().decode("utf-8"))
    try:
        os.makedirs(os.path.dirname(cache), exist_ok=True)
        with open(cache, "w", encoding="utf-8") as f:
            json.dump(data, f)
    except OSError:
        pass
    return data


def rings(geom):
    """Every ring of a Polygon/MultiPolygon, outer and holes together.

    The fill uses the even-odd rule, so holes (the Caspian, the great lakes)
    come out as water without being tracked separately."""
    out = []
    if geom["type"] == "Polygon":
        out.extend(geom["coordinates"])
    elif geom["type"] == "MultiPolygon":
        for poly in geom["coordinates"]:
            out.extend(poly)
    return out


def clip_ring(ring, w, s, e, n):
    """Sutherland-Hodgman clip of a ring to the map box."""
    def clip_edge(pts, inside, intersect):
        if not pts:
            return []
        out = []
        prev = pts[-1]
        prev_in = inside(prev)
        for cur in pts:
            cur_in = inside(cur)
            if cur_in:
                if not prev_in:
                    out.append(intersect(prev, cur))
                out.append(cur)
            elif prev_in:
                out.append(intersect(prev, cur))
            prev, prev_in = cur, cur_in
        return out

    def ix(x):
        def f(p, q):
            t = (x - p[0]) / (q[0] - p[0]) if q[0] != p[0] else 0.0
            return (x, p[1] + t * (q[1] - p[1]))
        return f

    def iy(y):
        def f(p, q):
            t = (y - p[1]) / (q[1] - p[1]) if q[1] != p[1] else 0.0
            return (p[0] + t * (q[0] - p[0]), y)
        return f

    pts = list(ring)
    pts = clip_edge(pts, lambda p: p[0] >= w, ix(w))
    pts = clip_edge(pts, lambda p: p[0] <= e, ix(e))
    pts = clip_edge(pts, lambda p: p[1] >= s, iy(s))
    pts = clip_edge(pts, lambda p: p[1] <= n, iy(n))
    return pts


def area(ring):
    total = 0.0
    for i in range(len(ring)):
        x0, y0 = ring[i]
        x1, y1 = ring[(i + 1) % len(ring)]
        total += x0 * y1 - x1 * y0
    return abs(total) * 0.5


def simplify(pts, eps):
    """Douglas-Peucker on an open point list; rings are cut at the far point
    first so the join is not smoothed away."""
    if len(pts) < 4:
        return pts
    keep = [False] * len(pts)
    keep[0] = keep[-1] = True
    stack = [(0, len(pts) - 1)]
    while stack:
        a, b = stack.pop()
        ax, ay = pts[a]
        bx, by = pts[b]
        dx, dy = bx - ax, by - ay
        den = math.hypot(dx, dy)
        best, best_i = -1.0, -1
        for i in range(a + 1, b):
            px, py = pts[i]
            if den == 0:
                d = math.hypot(px - ax, py - ay)
            else:
                d = abs(dy * px - dx * py + bx * ay - by * ax) / den
            if d > best:
                best, best_i = d, i
        if best > eps:
            keep[best_i] = True
            stack.append((a, best_i))
            stack.append((best_i, b))
    return [p for p, k in zip(pts, keep) if k]


def ring_path(ring):
    """Simplify a closed ring by cutting it at its most distant point."""
    if len(ring) < 8:
        return ring
    ring = ring[:-1] if ring[0] == ring[-1] else ring
    far = max(range(len(ring)),
              key=lambda i: (ring[i][0] - ring[0][0]) ** 2 +
                            (ring[i][1] - ring[0][1]) ** 2)
    a = ring[:far + 1]
    b = ring[far:] + [ring[0]]
    out = simplify(a, SIMPLIFY)[:-1] + simplify(b, SIMPLIFY)[:-1]
    return out


def inside_rings(pt, all_rings):
    """Even-odd point-in-rings test against the unclipped source."""
    x, y = pt
    count = 0
    for ring in all_rings:
        n = len(ring)
        for i in range(n):
            x0, y0 = ring[i]
            x1, y1 = ring[(i + 1) % n]
            if (y0 > y) != (y1 > y):
                xi = x0 + (y - y0) * (x1 - x0) / (y1 - y0)
                if xi > x:
                    count += 1
    return count % 2 == 1


def snap_to_land(lon, lat, all_rings):
    if inside_rings((lon, lat), all_rings):
        return lon, lat, True
    best = None
    for step in range(1, 41):
        r = step * 0.12
        for k in range(24):
            a = k * math.pi / 12
            x = lon + r * math.cos(a) / max(0.25, math.cos(math.radians(lat)))
            y = lat + r * math.sin(a)
            if WEST <= x <= EAST and SOUTH <= y <= NORTH and \
                    inside_rings((x, y), all_rings):
                d = (x - lon) ** 2 + (y - lat) ** 2
                if best is None or d < best[0]:
                    best = (d, x, y)
        if best is not None:
            return best[1], best[2], False
    return lon, lat, False


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else \
        "include/solis/apps/ck3_map.h"

    data = fetch(None)
    raw_rings = []
    for feature in data["features"]:
        raw_rings.extend(rings(feature["geometry"]))

    # Rings are kept whole rather than cut to the box: a clipped ring can
    # change parity along the cut and fill sea as land, while the scanline
    # fill below handles points outside the box correctly. Rings whose
    # bounding box misses the map go away, everything else is simplified.
    clipped = []
    for ring in raw_rings:
        if len(ring) < 4:
            continue
        xs = [p[0] for p in ring]
        ys = [p[1] for p in ring]
        if max(xs) < WEST - 1 or min(xs) > EAST + 1:
            continue
        if max(ys) < SOUTH - 1 or min(ys) > NORTH + 1:
            continue
        c = ring_path([(float(p[0]), float(p[1])) for p in ring])
        if len(c) < 3 or area(c) < MIN_AREA:
            continue
        clipped.append(c)

    # Order by area so the file reads big shapes first; the fill is even-odd
    # so the order does not affect the result.
    clipped.sort(key=area, reverse=True)

    prov_rows = []
    owner = 0
    prov_index = 0
    for realm in REALMS:
        for (pname, lon, lat, terrain, dev) in realm["provs"]:
            slon, slat, ok = snap_to_land(lon, lat, raw_rings)
            if not ok:
                print("  snapped %-16s %.2f,%.2f -> %.2f,%.2f"
                      % (pname, lon, lat, slon, slat))
            prov_rows.append((pname, slon, slat, terrain, dev, owner,
                              prov_index))
            prov_index += 1
        owner += 1

    out = []
    out.append("/* Generated by tools/gen_ck3_map.py - do not edit by hand.\n"
               " *\n"
               " * Coastlines: Natural Earth 1:50m land, clipped to\n"
               " * %.0fE..%.0fE, %.0fN..%.0fN and simplified. Coordinates are\n"
               " * tenths of a degree. Provinces are Voronoi seeds: every land\n"
               " * pixel is assigned to its nearest seed at load time.\n"
               " */\n" %
               (WEST, EAST, SOUTH, NORTH))
    out.append("#ifndef SOLIS_APPS_CK3_MAP_H\n#define SOLIS_APPS_CK3_MAP_H\n\n")
    out.append("#include <solis/types.h>\n\n")
    out.append("struct ck3_geo {\n    int16_t lon;\n    int16_t lat;\n};\n\n")
    out.append("struct ck3_shape {\n    const struct ck3_geo *pts;\n"
               "    int count;\n};\n\n")
    out.append("#define CK3_MAP_W %d\n#define CK3_MAP_H %d\n\n" %
               (MAP_W, MAP_H))
    out.append("/* Map box in tenths of a degree. */\n")
    out.append("#define CK3_LON_MIN %d\n#define CK3_LON_MAX %d\n"
               "#define CK3_LAT_MIN %d\n#define CK3_LAT_MAX %d\n\n" %
               (round(WEST * 10), round(EAST * 10),
                round(SOUTH * 10), round(NORTH * 10)))

    total_pts = 0
    for i, ring in enumerate(clipped):
        out.append("static const struct ck3_geo ck3_ring_%d[] = {\n" % i)
        line = "   "
        for (x, y) in ring:
            tok = " {%d,%d}," % (round(x * 10), round(y * 10))
            if len(line) + len(tok) > 78:
                out.append(line + "\n")
                line = "   "
            line += tok
            total_pts += 1
        out.append(line + "\n};\n\n")

    out.append("static const struct ck3_shape ck3_shores[] = {\n")
    for i, ring in enumerate(clipped):
        out.append("    {ck3_ring_%d, %d},\n" % (i, len(ring)))
    out.append("};\n#define CK3_SHORE_COUNT "
               "((int)(sizeof(ck3_shores) / sizeof(ck3_shores[0])))\n\n")

    out.append("struct ck3_prov_def {\n"
               "    const char *name;\n"
               "    int16_t lon;\n"
               "    int16_t lat;\n"
               "    uint8_t terrain;\n"
               "    uint8_t dev;\n"
               "    uint8_t owner;\n"
               "};\n\n")
    out.append("static const struct ck3_prov_def ck3_prov_defs[] = {\n")
    for (pname, lon, lat, terrain, dev, own, _) in prov_rows:
        out.append('    {"%s", %d, %d, %d, %d, %d},\n' %
                   (pname, round(lon * 10), round(lat * 10),
                    terrain, dev, own))
    out.append("};\n")
    out.append("#define CK3_PROV_COUNT "
               "((int)(sizeof(ck3_prov_defs) / sizeof(ck3_prov_defs[0])))\n\n")

    out.append("struct ck3_realm_def {\n"
               "    const char *name;\n"
               "    const char *label;\n"
               "    const char *ruler;\n"
               "    const char *house;\n"
               "    uint32_t color;\n"
               "    uint8_t faith;\n"
               "    uint8_t aggression;\n"
               "    uint8_t capital;\n"
               "    int16_t label_lon;\n"
               "    int16_t label_lat;\n"
               "};\n\n")
    out.append("static const struct ck3_realm_def ck3_realm_defs[] = {\n")
    capital = 0
    for realm in REALMS:
        lon, lat = realm["at"]
        out.append('    {"%s", "%s", "%s", "%s", 0xFF%06X, %d, %d, %d, '
                   '%d, %d},\n' %
                   (realm["name"], realm["label"], realm["ruler"],
                    realm["house"], realm["color"], realm["faith"],
                    realm["aggr"], capital,
                    round(lon * 10), round(lat * 10)))
        capital += len(realm["provs"])
    out.append("};\n")
    out.append("#define CK3_REALM_COUNT "
               "((int)(sizeof(ck3_realm_defs) / sizeof(ck3_realm_defs[0])))\n\n")
    out.append("#endif\n")

    with open(out_path, "w", encoding="utf-8") as f:
        f.write("".join(out))

    print("rings %d, points %d, provinces %d, realms %d"
          % (len(clipped), total_pts, len(prov_rows), len(REALMS)))
    print("wrote", out_path)


if __name__ == "__main__":
    main()
