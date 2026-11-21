#!/usr/bin/env python3
"""Publish the built .spx packages to the GitHub repo that hosts them.

This is the host half of `make commit`: the guest cannot hold a GitHub token
and Solis' network stack is read-only, so publishing runs where the
credentials live. For every package listed in pkg/repo.json the tool

  * uploads pkg/<dir>/<name>.spx to os-maxxer/NPK,
  * deletes the old .npx it replaces (root and package directory),
  * uploads the updated repo.json manifest.

Authentication: GITHUB_TOKEN (or GH_TOKEN), else `gh auth token`. The token
needs `contents: write` on the repository.

Usage:
    python3 tools/npk_commit.py                 # publish
    python3 tools/npk_commit.py --dry-run       # show what would change
    python3 tools/npk_commit.py --only browser  # one package
    python3 tools/npk_commit.py --no-delete     # keep legacy .npx files
"""
import argparse
import base64
import json
import os
import subprocess
import sys
import urllib.error
import urllib.request

DEFAULT_REPO = "os-maxxer/NPK"
API = "https://api.github.com"


def find_token():
    for name in ("GITHUB_TOKEN", "GH_TOKEN"):
        value = os.environ.get(name)
        if value:
            return value.strip()
    try:
        out = subprocess.run(["gh", "auth", "token"], capture_output=True,
                             text=True, timeout=20)
        if out.returncode == 0 and out.stdout.strip():
            return out.stdout.strip()
    except (OSError, subprocess.SubprocessError):
        pass
    return None


class GitHub:
    def __init__(self, repo, token, dry_run=False):
        self.repo = repo
        self.token = token
        self.dry_run = dry_run

    def request(self, method, path, payload=None):
        url = "%s/repos/%s/contents/%s" % (API, self.repo, path.lstrip("/"))
        data = None
        if payload is not None:
            data = json.dumps(payload).encode("utf-8")
        req = urllib.request.Request(url, data=data, method=method)
        req.add_header("Accept", "application/vnd.github+json")
        req.add_header("User-Agent", "solis-npk-commit")
        if self.token:
            req.add_header("Authorization", "Bearer " + self.token)
        try:
            with urllib.request.urlopen(req, timeout=45) as resp:
                body = resp.read()
                return resp.status, json.loads(body) if body else None
        except urllib.error.HTTPError as exc:
            body = exc.read()
            try:
                detail = json.loads(body).get("message", "")
            except Exception:
                detail = body.decode("utf-8", "replace")[:200]
            return exc.code, {"message": detail}

    def get_sha(self, path):
        status, body = self.request("GET", path)
        if status == 200 and isinstance(body, dict):
            return body.get("sha")
        if status == 404:
            return None
        raise RuntimeError("GET %s failed: %s %s" %
                           (path, status, (body or {}).get("message", "")))

    def put(self, path, content, message):
        sha = self.get_sha(path)
        payload = {
            "message": message,
            "content": base64.b64encode(content).decode("ascii"),
        }
        if sha:
            payload["sha"] = sha
        if self.dry_run:
            print("  [dry-run] %s %s (%d bytes)" %
                  ("update" if sha else "create", path, len(content)))
            return
        status, body = self.request("PUT", path, payload)
        if status not in (200, 201):
            raise RuntimeError("PUT %s failed: %s %s" %
                               (path, status, (body or {}).get("message", "")))
        print("  %s %s (%d bytes)" %
              ("updated" if sha else "created", path, len(content)))

    def delete(self, path):
        sha = self.get_sha(path)
        if not sha:
            return False
        if self.dry_run:
            print("  [dry-run] delete %s" % path)
            return True
        status, body = self.request("DELETE", path, {
            "message": "Replace %s with .spx" % path,
            "sha": sha,
        })
        if status not in (200, 204):
            raise RuntimeError("DELETE %s failed: %s %s" %
                               (path, status, (body or {}).get("message", "")))
        print("  deleted %s" % path)
        return True


def load_manifest(path):
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo", default=os.environ.get("NPK_REPO", DEFAULT_REPO),
                        help="GitHub repository, default %s" % DEFAULT_REPO)
    parser.add_argument("--manifest", default="pkg/repo.json",
                        help="local manifest, default pkg/repo.json")
    parser.add_argument("--remote-manifest", default="repo.json",
                        help="manifest path inside the repository")
    parser.add_argument("--only", default="",
                        help="comma separated package names")
    parser.add_argument("--no-delete", action="store_true",
                        help="keep the legacy .npx files")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    with open(args.manifest, "r", encoding="utf-8") as handle:
        raw = handle.read()
    manifest = json.loads(raw)

    token = find_token()
    if not token and not args.dry_run:
        print("No GitHub token. Set GITHUB_TOKEN (contents:write) or run "
              "`gh auth login`.", file=sys.stderr)
        return 2

    only = [n.strip().lower() for n in args.only.split(",") if n.strip()]
    api = GitHub(args.repo, token, args.dry_run)

    missing = []
    published = 0
    for pkg in manifest.get("packages", []):
        name = pkg["name"]
        if only and name.lower() not in only:
            continue
        rel = pkg.get("file", "%s/%s.spx" % (name, name))
        local = os.path.join("pkg", rel)
        if not os.path.isfile(local):
            missing.append(local)
            continue
        with open(local, "rb") as handle:
            content = handle.read()
        if len(content) < 48:
            print("Skipping %s: not an SPX image" % local, file=sys.stderr)
            missing.append(local)
            continue
        print("%s -> %s" % (local, rel))
        api.put(rel, content, "Publish %s %s as .spx" %
                (name, pkg.get("version", "")))
        published += 1

        if args.no_delete:
            continue
        legacy = pkg.get("legacy")
        legacy_paths = []
        if legacy:
            legacy_paths.append(legacy)
        legacy_paths.append("%s.npx" % name)
        legacy_paths.append(os.path.join(os.path.dirname(rel), "%s.npx" % name))
        seen = set()
        for path in legacy_paths:
            path = path.replace("\\", "/").lstrip("/")
            if path in seen or path.endswith(".spx"):
                continue
            seen.add(path)
            if api.delete(path):
                print("  replaced %s" % path)

    if published:
        print("Updating %s" % args.remote_manifest)
        api.put(args.remote_manifest, raw.encode("utf-8"),
                "Update package manifest for .spx layout")

    if missing:
        print("Not published (build them with `make pkg-spx`): %s" %
              ", ".join(missing), file=sys.stderr)
        return 1
    if not published:
        print("Nothing to publish.", file=sys.stderr)
        return 1
    print("%s %d package(s) to %s%s" %
          ("Would publish" if args.dry_run else "Published",
           published, args.repo, " (dry run)" if args.dry_run else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
