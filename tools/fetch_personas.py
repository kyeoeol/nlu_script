"""Samples personas from nvidia/Nemotron-Personas-Korea for the demo data.

    python3 tools/fetch_personas.py > examples/personas/personas.jsonl

Picks 100 rows of the dataset's 1,000,000 at offsets drawn from a fixed
seed, so the sample is the same on every run, and fetches them one by one
from the Hugging Face dataset viewer API (no login or download of the
dataset needed). Each output line keeps the attributes the demo sentences
are written from: sex, age, family, education, occupation, region and the
one-sentence persona summary.

The dataset is CC BY 4.0 (https://huggingface.co/datasets/nvidia/Nemotron-Personas-Korea).
"""
import json
import random
import sys
import time
import urllib.request

DATASET = "nvidia/Nemotron-Personas-Korea"
ROWS = 1000000
COUNT = 100
SEED = 20261030
FIELDS = ["sex", "age", "marital_status", "family_type", "housing_type",
          "education_level", "occupation", "province", "district", "persona"]
URL = ("https://datasets-server.huggingface.co/rows?dataset=%s"
       "&config=default&split=train&offset=%d&length=1")


def fetch(offset):
    # The API answers 429 after a few dozen quick requests; back off and retry.
    for attempt in range(8):
        try:
            with urllib.request.urlopen(URL % (DATASET, offset), timeout=30) as response:
                return json.load(response)["rows"][0]["row"]
        except Exception:
            if attempt == 7:
                raise
            time.sleep(min(60, 5 * 2 ** attempt))


def main():
    offsets = sorted(random.Random(SEED).sample(range(ROWS), COUNT))
    for number, offset in enumerate(offsets, 1):
        if number > 1:
            time.sleep(2)
        row = fetch(offset)
        persona = {"id": number, "offset": offset, "uuid": row["uuid"]}
        persona.update((field, row[field]) for field in FIELDS)
        sys.stdout.write(json.dumps(persona, ensure_ascii=False) + "\n")


if __name__ == "__main__":
    main()
