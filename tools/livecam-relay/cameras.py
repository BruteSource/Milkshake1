"""Roster of live cams: all officially operated by the explore.org nature
cam network (or its named partners -- NPS Katmai, Africam, etc.), verified
`is_live` at compile time via `yt-dlp --flat-playlist -j <channel>/streams`.
No aggregator/scraped sources. Re-run tools/livecam-relay/discover.py to
refresh -- streams do go offline/get replaced by the network periodically.

format: youtube-dl format id for a ~240p video-only stream (see `yt-dlp -F
<url>` per video; explore.org's channels consistently expose 229 =
426x240 h264, close to this board's 320x240 panel).
"""

CAMERAS = [
    # -- Bears (Katmai National Park + Anan Creek, Alaska) --
    {"id": "brooks-falls", "category": "Bears", "title": "Brooks Falls Brown Bears",
     "youtube_url": "https://www.youtube.com/watch?v=J7ZrIDvqlic", "format": "229"},
    {"id": "brooks-falls-low", "category": "Bears", "title": "Brooks Falls Low Angle",
     "youtube_url": "https://www.youtube.com/watch?v=EwTH5yY7Mks", "format": "229"},
    {"id": "brooks-riffles", "category": "Bears", "title": "Brooks River Riffles",
     "youtube_url": "https://www.youtube.com/watch?v=z7_GhJeFxQI", "format": "229"},
    {"id": "brooks-river-watch", "category": "Bears", "title": "Brooks River Watch",
     "youtube_url": "https://www.youtube.com/watch?v=wkVLYfU-Kew", "format": "229"},
    {"id": "brooks-underwater", "category": "Bears", "title": "Brooks Underwater Salmon",
     "youtube_url": "https://www.youtube.com/watch?v=vu7I315gQpU", "format": "229"},
    {"id": "kats-river-view", "category": "Bears", "title": "Kat's River View",
     "youtube_url": "https://www.youtube.com/watch?v=cTsjMtjRLCo", "format": "229"},
    {"id": "anan-lower-falls", "category": "Bears", "title": "Anan Lower Falls & Caves",
     "youtube_url": "https://www.youtube.com/watch?v=RhP-_jX8-Zs", "format": "229"},
    {"id": "anan-fishing-3", "category": "Bears", "title": "Anan Fishing Hole 3",
     "youtube_url": "https://www.youtube.com/watch?v=G839-Yj4Cvo", "format": "229"},
    {"id": "anan-fishing-4", "category": "Bears", "title": "Anan Fishing Hole 4",
     "youtube_url": "https://www.youtube.com/watch?v=ypMu3yA7h3s", "format": "229"},
    {"id": "anan-forest-view", "category": "Bears", "title": "Anan Forest View",
     "youtube_url": "https://www.youtube.com/watch?v=RjrRz3t5kO4", "format": "229"},

    # -- Africa (Masai Mara, Tembe, Africam network) --
    {"id": "tembe-elephant", "category": "Africa", "title": "Tembe Elephant Park",
     "youtube_url": "https://www.youtube.com/watch?v=0P_LBKqVbfs", "format": "229"},
    {"id": "mara-main-crossing", "category": "Africa", "title": "Masai Mara Main Crossing",
     "youtube_url": "https://www.youtube.com/watch?v=-7GOA9KIWcs", "format": "229"},
    {"id": "mara-fig-tree", "category": "Africa", "title": "Masai Mara Fig Tree Crossing",
     "youtube_url": "https://www.youtube.com/watch?v=Io1Kle7QR-E", "format": "229"},
    {"id": "kalahari-salt-pan", "category": "Africa", "title": "Kalahari Salt Pan",
     "youtube_url": "https://www.youtube.com/watch?v=epZP0VOirh0", "format": "229"},
    {"id": "boteti-zebra", "category": "Africa", "title": "Boteti River Zebra Migration",
     "youtube_url": "https://www.youtube.com/watch?v=7hKbyXxWT2k", "format": "229"},
    {"id": "africam-tau", "category": "Africa", "title": "Africam Tau Waterhole",
     "youtube_url": "https://www.youtube.com/watch?v=DsNtwGJXTTs", "format": "229"},
    {"id": "africam-nkorho", "category": "Africa", "title": "Africam Nkorho Bush Lodge",
     "youtube_url": "https://www.youtube.com/watch?v=dIChLG4_WNs", "format": "229"},
    {"id": "africam-olifants", "category": "Africa", "title": "Africam Olifants River",
     "youtube_url": "https://www.youtube.com/watch?v=_NXaovxB-Bk", "format": "229"},
    {"id": "africam-naledi", "category": "Africa", "title": "Africam Naledi Cat-EYE",
     "youtube_url": "https://www.youtube.com/watch?v=pZZst4BOpVI", "format": "229"},
    {"id": "africam-rosies-pan", "category": "Africa", "title": "Africam Rosie's Pan",
     "youtube_url": "https://www.youtube.com/watch?v=ItdXaWUVF48", "format": "229"},

    # -- Ocean (Florida springs, Utopia Village reef, California coast) --
    {"id": "homosassa-manatee", "category": "Ocean", "title": "Homosassa Manatee Cam",
     "youtube_url": "https://www.youtube.com/watch?v=Fz6sl9YJZE0", "format": "229"},
    {"id": "tropical-reef", "category": "Ocean", "title": "Tropical Reef Tank",
     "youtube_url": "https://www.youtube.com/watch?v=DHUnz4dyb54", "format": "229"},
    {"id": "silver-springs-above", "category": "Ocean", "title": "Silver Springs Manatee (Above)",
     "youtube_url": "https://www.youtube.com/watch?v=jxnehowX-9Y", "format": "229"},
    {"id": "silver-springs-180", "category": "Ocean", "title": "Silver Springs Manatee (180)",
     "youtube_url": "https://www.youtube.com/watch?v=zPqPFZMGTF8", "format": "229"},
    {"id": "utopia-back-channel", "category": "Ocean", "title": "Utopia Village Back Channel Reef",
     "youtube_url": "https://www.youtube.com/watch?v=nmjlQlYygB4", "format": "229"},
    {"id": "utopia-sandy-channel", "category": "Ocean", "title": "Utopia Village Sandy Channel Reef",
     "youtube_url": "https://www.youtube.com/watch?v=jzx_n25g3kA", "format": "229"},
    {"id": "utopia-front-dock", "category": "Ocean", "title": "Utopia Village Front-of-Dock Reef",
     "youtube_url": "https://www.youtube.com/watch?v=Kf-x20Yq0_A", "format": "229"},
    {"id": "utopia-edge-wall", "category": "Ocean", "title": "Utopia Village Edge-of-Wall Reef",
     "youtube_url": "https://www.youtube.com/watch?v=Sq-X4Ga1oyc", "format": "229"},
    {"id": "utopia-top-wall", "category": "Ocean", "title": "Utopia Village Top-of-Wall Reef",
     "youtube_url": "https://www.youtube.com/watch?v=1zcIUk66HX4", "format": "229"},
    {"id": "utopia-back-dock", "category": "Ocean", "title": "Utopia Village Back-of-Dock Reef",
     "youtube_url": "https://www.youtube.com/watch?v=Lv9t0hZTvz4", "format": "229"},
    {"id": "anacapa-ocean", "category": "Ocean", "title": "Anacapa Channel Islands NP",
     "youtube_url": "https://www.youtube.com/watch?v=OAJF1Ie1m_Q", "format": "229"},
    {"id": "catalina-marine", "category": "Ocean", "title": "USC Wrigley Catalina Marine Reserve",
     "youtube_url": "https://www.youtube.com/watch?v=JH_NzhSsqis", "format": "229"},

    # -- Other --
    {"id": "decorah-eagles", "category": "Birds & More", "title": "Decorah Eagles North Nest",
     "youtube_url": "https://www.youtube.com/watch?v=GGIE1E-kaMQ", "format": "229"},
    {"id": "wolf-center", "category": "Birds & More", "title": "International Wolf Center",
     "youtube_url": "https://www.youtube.com/watch?v=5e4lsEe4Vew", "format": "229"},
    {"id": "kitten-rescue", "category": "Birds & More", "title": "Kitten Rescue Cat Cam",
     "youtube_url": "https://www.youtube.com/watch?v=-m_nQT62B4Y", "format": "229"},
]

CATEGORIES = sorted({c["category"] for c in CAMERAS})
