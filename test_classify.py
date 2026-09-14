"""
Quick test for the classification function using real samples from the feed.
Requires the AI_API_KEYS (or AI_API_KEY) environment variable to be set.
"""

import monitor

samples = [
    {"title": "No 10 Modification de l'horaire des cours intensifs de francais et d'anglais",
     "summary": "Please follow this link to read the notice"},
    {"title": "No 9 Genie Electrique - Oraux probatoires",
     "summary": "Please follow this link to read the notice"},
    {"title": "No 5 Offre d'emploi: Comptable / Auditeur (Dekwaneh)",
     "summary": "Please follow this link to read the notice"},
]

for s in samples:
    category = monitor.classify_announcement(s)
    print(f"{s['title'][:60]:60}  ->  {category}")
