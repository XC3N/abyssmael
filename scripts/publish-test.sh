#!/bin/sh
# Merge main and every open PR into a throwaway branch, build host+device+sim, and publish it as the site's "test" build
# (firmware/abyssmael/test/test/ and the "test" entry in firmware/firmwares.json), pushing the merge as branch test-build.
# Needs gh, clang (wasm32), fm1-base (see BUILD.md). Run from a clean checkout of main.
set -e
cd "$(dirname "$0")/.."
git checkout -q main && git pull -q --rebase
git checkout -q -B test-all origin/main
for ref in $(gh api 'repos/XC3N/abyssmael/pulls?state=open' --jq '.[].head.ref'); do
  git fetch -q origin "$ref"
  if ! git merge --no-edit FETCH_HEAD >/dev/null 2>&1; then
    # the one conflict we resolve ourselves: PRs that each bump STORE_MAGIC (keep the highest); anything else stops the publish
    if [ "$(git diff --name-only --diff-filter=U)" = "src/abyssmael.h" ] && python3 scripts/resolve-magic.py src/abyssmael.h; then
      git add src/abyssmael.h && git commit -q --no-edit && echo "  (resolved STORE_MAGIC conflict with $ref: kept the highest)"
    else
      echo "merge conflict with $ref"; git merge --abort; git checkout -q main; exit 1
    fi
  fi
  echo "merged $ref"
done
COMMIT=$(git rev-parse --short HEAD)
BUILD_LABEL=test scripts/build-fw.sh all | tail -n 7
T=/tmp/test-pub; rm -rf $T; mkdir -p $T
cp build-out/abyssmael.fwsc $T/
# the player gets the same build tag as the package (a copy of src/ with the tag line rewritten)
TAG=$(cat build-out/tag.txt); rm -rf /tmp/test-src; cp -r src /tmp/test-src
sed -i "s|^#define AMB_BUILD_TAG .*|#define AMB_BUILD_TAG \"$TAG\"|" /tmp/test-src/app.h
VERSION_TAG=${TAG%% *}
SIM_MEM=4194304 SIM_UNITS="core.c seq.c engines.c fx.c" sh scripts/build-sim.sh /tmp/test-src firmware/abyssmael/sim/sim.c $T/sim.wasm
git push -q -f origin test-all:test-build
git checkout -q main
mkdir -p firmware/abyssmael/test/test; cp $T/abyssmael.fwsc $T/sim.wasm firmware/abyssmael/test/test/
SHA=$(sha256sum $T/abyssmael.fwsc | cut -d' ' -f1)
WSHA=$(sha256sum $T/sim.wasm | cut -d' ' -f1)
PRS=$(gh api 'repos/XC3N/abyssmael/pulls?state=open' --jq '[.[]|"#\(.number)"]|join(", ")')
python3 - "$COMMIT" "$SHA" "$PRS" "${VERSION_TAG#v}" "$WSHA" <<'P'
import json,sys,datetime
c,s,prs,ver,ws=sys.argv[1:6]
p='firmware/firmwares.json'; d=json.load(open(p))
e=[x for x in d['firmwares'] if x['id']=='abyssmael'][0]
t=[x for x in e.get('tests',[]) if x['label']!='test']
t.append({"label":"test","version":ver+" (test)","commit":c,"built":datetime.date.today().isoformat(),
 "file":"abyssmael/test/test/abyssmael.fwsc","sha256":s,"wasm":"abyssmael/test/test/sim.wasm","wasmSha256":ws,
 "notes":"main plus the open PRs ("+prs+"), merged as branch test-build"})
e['tests']=t
json.dump(d,open(p,'w'),indent=2); open(p,'a').write('\n')
P
git add -A firmware
git commit -q -m "Republish the test build (main plus open PRs $PRS, test-build $COMMIT)

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01YHfRPx5emRB7Ze4KE2gLGL"
git pull -q --rebase && git push -q origin main
echo "published test $COMMIT sha256 $SHA"
