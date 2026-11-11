# Preprocesses one Markdown file of the GitHub docs for the VCV Library manual (manual/).
# Invoked by plugin-manual.mk on `make manual` / `make dist`. POSIX awk only, no extensions,
# so it runs with the awk of the Rack plugin toolchain, macOS and MSYS2.
#
#   awk -v src=docs/arena/Arena.md -v docs=docs -v github=<blob url> -v assets=<list file> \
#       [-v skipext="syx|mid"] -f manual.awk <file>
#
# `src` is the file's path relative to the plugin root, `docs` the docs folder, `github` the URL
# prefix for repository files that are not part of the manual.
#
# Markers (HTML comments, invisible on GitHub), each on a line of its own:
#
#   <!-- manual:exclude -->           content dropped from the manual
#   <!-- /manual:exclude -->
#
#   <!-- manual:only                  content that appears in the manual only
#   -->
#
# All other HTML comments are removed as well (the VCV Library does not render raw HTML).
#
# Links: the manual is flat (manual/<Page>.md), so relative links are rewritten:
#   <docs>/.../<Page>.md          -> <Page>.md
#   <docs>/.../<image>            -> images/<image> (png, jpg, gif, svg, webp)
#   <docs>/<path>/<file>          -> <path>/<file>  (other files)
#   files with an extension in `skipext` (e.g. "syx|mid") are not copied, link reduced to text
#   both are appended as "<dest>\t<source>" to the `assets` list for copying
#   README.md, CHANGELOG.md       -> plugin.md, changelog.md
#   any other repo file           -> absolute GitHub URL
# External links ([text](https://...), including the GitHub URLs above, and YouTube) are reduced
# to their text, and lines holding only a URL (the Library embeds these as videos) are dropped.
# Inside manual:only blocks external links and URL lines are kept. Images always stay.
# Links inside fenced code blocks are left untouched.

function fail(msg) {
	printf("%s:%d: error: %s\n", src, FNR, msg) > "/dev/stderr"
	failed = 1
	exit 1
}

# Joins a relative link onto the source directory and resolves "." and ".." segments.
function resolve(dir, path,    n, i, seg, out, m) {
	n = split((dir == "" ? "" : dir "/") path, seg, "/")
	m = 0
	for (i = 1; i <= n; i++) {
		if (seg[i] == "" || seg[i] == ".") continue
		if (seg[i] == ".." && m > 0 && out[m] != "..") { m--; continue }
		out[++m] = seg[i]
	}
	path = ""
	for (i = 1; i <= m; i++) path = path (i > 1 ? "/" : "") out[i]
	return path
}

function rewrite(target,    path, anchor, p, i, file, dest) {
	# Absolute URLs (scheme:), in-page anchors and site-absolute paths stay as they are
	if (target ~ /^[A-Za-z][A-Za-z0-9+.-]*:/ || target ~ /^#/ || target ~ /^\//) return target
	anchor = ""
	i = index(target, "#")
	if (i > 0) { anchor = substr(target, i); path = substr(target, 1, i - 1) } else path = target
	p = resolve(srcdir, path)
	if (p ~ /^\.\.\//) return target
	if (p == "README.md") return "plugin.md" anchor
	if (p == "CHANGELOG.md") return "changelog.md" anchor
	if (index(p, docs "/") == 1 && p ~ /\.md$/) { sub(/^.*\//, "", p); return p anchor }
	if (index(p, docs "/") == 1) {
		# Excluded file types are not copied; the link is reduced to its text by links()
		if (skipext != "" && tolower(p) ~ ("\\.(" skipext ")$")) return ""
		file = p
		gsub(/%20/, " ", file)
		# Images go flat into images/, other files keep their folder below the docs folder
		if (tolower(p) ~ /\.(png|jpe?g|gif|svg|webp)$/) sub(/^.*\//, "images/", p)
		else p = substr(p, length(docs) + 2)
		dest = p
		gsub(/%20/, " ", dest)
		print dest "\t" file >> assets
		return p anchor
	}
	return github p anchor
}

# Position of the "[" opening the link text that ends with the "]" at the end of `s`, 0 if none.
function opening(s,    i, c, depth) {
	depth = 0
	for (i = length(s) - 1; i > 0; i--) {
		c = substr(s, i, 1)
		if (c == "]") depth++
		else if (c == "[") { if (depth == 0) return i; depth-- }
	}
	return 0
}

# Rewrites every [text](target) / ![alt](target "title") on a line. Links to external sites
# (any URL scheme, including GitHub URLs produced by rewrite()) are unwrapped to their text,
# unless `external` is set. Images are never removed.
function links(line, external,    out, t, j, target, rest, i) {
	out = ""
	while (match(line, /\]\([^)]*\)/)) {
		out = out substr(line, 1, RSTART)
		t = substr(line, RSTART + 2, RLENGTH - 3)
		line = substr(line, RSTART + RLENGTH)
		sub(/^[ \t]+/, "", t)
		j = index(t, " ")
		if (j > 0) { target = substr(t, 1, j - 1); rest = substr(t, j) } else { target = t; rest = "" }
		target = rewrite(target)
		if (target == "" || (!external && target ~ /^[A-Za-z][A-Za-z0-9+.-]*:/)) {
			i = opening(out)
			if (i > 0 && substr(out, i - 1, 1) != "!") {
				out = substr(out, 1, i - 1) substr(out, i + 1, length(out) - i - 1)
				continue
			}
		}
		out = out "(" target rest ")"
	}
	return out line
}

# Removes HTML comments, also across lines (state kept in `incomment`).
function uncomment(line,    out, i) {
	out = ""
	while (line != "") {
		if (incomment) {
			i = index(line, "-->")
			if (i == 0) return out
			line = substr(line, i + 3)
			incomment = 0
		} else {
			i = index(line, "<!--")
			if (i == 0) return out line
			out = out substr(line, 1, i - 1)
			line = substr(line, i + 4)
			incomment = 1
		}
	}
	return out
}

function emit(line) {
	if (line ~ /^[ \t]*$/) {
		# Collapse runs of blank lines (e.g. left behind by removed sections) outside of code
		if (!fence && (blank || !printed)) return
		blank = 1
	} else blank = 0
	print line
	printed = 1
}

BEGIN {
	srcdir = src
	if (!sub(/\/[^\/]*$/, "", srcdir)) srcdir = ""
	mode = ""      # "", "exclude", "only"
	fence = 0
	incomment = 0
}

{
	line = $0
	sub(/\r$/, "", line)
	isfence = (line ~ /^[ \t]*(```|~~~)/)

	if (mode == "" && !fence && !incomment) {
		if (line ~ /^[ \t]*<!--[ \t]*manual:exclude[ \t]*-->[ \t]*$/) { mode = "exclude"; start = FNR; next }
		if (line ~ /^[ \t]*<!--[ \t]*manual:only[ \t]*$/) { mode = "only"; start = FNR; next }
		if (line ~ /<!--[ \t]*\/?manual:/) fail("malformed or misplaced manual marker: " line)
	} else if (mode == "exclude") {
		if (line ~ /^[ \t]*<!--[ \t]*\/manual:exclude[ \t]*-->[ \t]*$/) { mode = ""; next }
		if (line ~ /<!--[ \t]*\/?manual:/) fail("nested manual marker (manual:exclude opened in line " start ")")
		next
	} else if (mode == "only") {
		if (line ~ /^[ \t]*-->[ \t]*$/) { mode = ""; next }
		if (line ~ /manual:/ && line ~ /<!--|-->/) fail("nested manual marker (manual:only opened in line " start ")")
	}

	if (fence) {
		if (isfence) fence = 0
		emit(line)
		next
	}
	if (isfence && !incomment) {
		fence = 1
		emit(line)
		next
	}

	if (mode != "only") {
		wascomment = incomment || index(line, "<!--") > 0
		line = uncomment(line)
		# A line that held nothing but a comment disappears completely
		if (wascomment && line ~ /^[ \t]*$/) next
		# A line holding only a URL is an embed (YouTube) or an autolink
		if (line ~ /^[ \t]*<?[A-Za-z][A-Za-z0-9+.-]*:\/\/[^ \t]*>?[ \t]*$/) next
	}
	emit(links(line, mode == "only"))
}

END {
	if (failed) exit 1
	if (mode != "") { FNR = start; fail("unterminated manual:" mode " block") }
	if (incomment) fail("unterminated HTML comment")
}
