"""The docs and the tree agree where a machine can tell: links, paths and names the pages give,
the third-party modules, the engine's layer order, and the rules the pages state of the code (no graphics API
outside a backend, one random draw to a call). Each area's size is reported, failing nothing.

Run by ctest as `docs: the docs and the tree agree`; or directly:
    python3 tests/docs/check_docs.py [engine root]
"""
import re
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(sys.argv.pop(1) if len(sys.argv) > 1 else Path(__file__).resolve().parents[2])
DOCS = ROOT / 'docs'
# The tree as git holds it, committed or not, less what it ignores (a build, a note of the owner's, a scratch
# folder), so the suite's answer is the same on every machine. A library under modules/ is a repository of its
# own, which git lists as one folder: OURS keeps files, and MODULES the folders under modules/.
LISTED = [f for f in subprocess.run(['git', '-C', str(ROOT), 'ls-files', '-z', '--cached', '--others',
                                     '--exclude-standard'], capture_output=True, check=True,
                                    text=True).stdout.split('\0') if f]
OURS = sorted(f for f in LISTED if (ROOT / f).is_file())
MODULES = sorted({f.split('/')[1] for f in LISTED if f.startswith('modules/')
                  and (ROOT / 'modules' / f.split('/')[1]).is_dir()})
PAGES = [ROOT / f for f in OURS if f.startswith('docs/') and f.endswith('.md')] + [ROOT / 'README.md', ROOT / 'CONTRIBUTING.md']
SOURCE_EXTENSIONS = ('.h', '.cpp')

# What names a graphics API: a backend's classes, the libraries' namespaces and headers, and the APIs' calls,
# types, enums and loaders. GLFW is the window library, not a graphics API (GLFWwindow, glfwPollEvents), and a
# library's name in the build info (vkmOpenGL) is no use of it.
GRAPHICS_API = re.compile('|'.join([
    r'\bGL[A-Z][a-z]\w*',                  # a backend class: GLBackend, GLAssetMirror
    r'\b(?:GL|OpenGL)::',                  # a library's namespace
    r'\bgl(?!TF\b)[A-Z]\w*\s*\(',          # a call: glDrawArrays(
    r'\bGL[a-z]\w*', r'\bGL_[A-Z]',        # a type or an enum: GLuint, GL_TEXTURE_2D
    r'glew|GLEW', r'\b(?:OpenGL|opengl\w*|EGL)\b',
    r'\b[Vv]ulkan\b', r'\bVk[A-Z]\w*', r'\bvk[A-Z]\w*\s*\(', r'\bVK_[A-Z]',
    r'#\s*include\s*[<"](?:GL/|gl_[^">]*\.h|glew|vulkan/|vkm_opengl/|vkm_vulkan/)',
]))
# A path into a backend's place cites it; it does not use the API.
BACKEND_CITATION = re.compile(r'\b(?:shaders|src/backend|tests(?:/\w+)*)/(?:opengl|vulkan)/[\w./-]*')
APIS = ('opengl', 'vulkan')


# The draws of the engine's random sources: Math::Rng's own, Math::Random's free ones, and, found in the tree,
# every function that returns one (a script's frand) or is handed a generator (boxPoint(rng, ...)).
RNG_DRAWS = {'nextU32', 'nextFloat', 'nextInt', 'nextBool'}
RANDOM_DRAWS = re.compile(r'\bRandom::(?:value|range|boolean)\s*\(')
NOT_A_CALL = {'if', 'while', 'for', 'switch', 'return', 'sizeof', 'alignof', 'decltype', 'catch',
              'static_assert'}


def code_only(source):
    """@p source with its comments, strings and character literals blanked, offsets kept."""
    blank = lambda m: re.sub(r'[^\n]', ' ', m.group(0))
    pattern = r'//[^\n]*|/\*.*?\*/|R"([^(]*)\(.*?\)\1"|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])+\''
    return re.sub(pattern, blank, source, flags=re.S)


def drawing_functions(sources):
    """Names that draw when called: one-line wrappers of a draw, and functions taking a Rng by reference.
    random.h's own are RNG_DRAWS and RANDOM_DRAWS, by their qualified names: value() and range() alone name
    other things too."""
    wraps, given = set(), set()
    for path, source in sources.items():
        if path.name == 'random.h':
            continue
        wraps |= set(re.findall(r'\b(\w+)\s*\([^()]*\)\s*(?:const\s*)?\{\s*return\s+[^;{}]*?\b'
                                r'(?:nextU32|nextFloat|nextInt|nextBool)\s*\(', source))
        given |= set(re.findall(r'\b(\w+)\s*\([^()]*\bRng\s*&', source))
    return wraps - {'Rng'}, given - {'Rng'}


def calls_drawing_twice(source, wraps, given):
    """(line, draws) of each call whose argument list holds two or more draws."""
    code = code_only(source)
    pairs, stack = {}, []
    for i, c in enumerate(code):
        if c == '(':
            stack.append(i)
        elif c == ')' and stack:
            pairs[stack.pop()] = i
    named = lambda i: re.search(r'([\w:]+)\s*$', code[max(0, i - 80):i])

    def draws(i):
        m = named(i)
        if not m:
            return False
        name = m.group(1).split('::')[-1]
        if name in RNG_DRAWS or name in wraps or RANDOM_DRAWS.search(code[max(0, i - 80):i + 1]):
            return True
        return name in given and re.search(r'rng', code[i:pairs[i]], re.I) is not None

    draw_at = sorted(i for i in pairs if draws(i))
    found = {}  # line: the most draws a call opened on it takes, so a call inside a call is said once
    for start, end in sorted(pairs.items()):
        m = named(start)
        if not m or m.group(1) in NOT_A_CALL:
            continue
        inside = sum(1 for d in draw_at if start < d < end)
        if inside >= 2:
            line = code.count('\n', 0, start) + 1
            found[line] = max(found.get(line, 0), inside)
    return sorted(found.items())


def in_a_backends_place(rel):
    """src/backend/<api>/, tests/**/<api>/ and the hosts' one choice: where a graphics API may be named."""
    parts = rel.split('/')
    if parts[:2] == ['src', 'backend'] and len(parts) > 2 and parts[2] in APIS:
        return True
    if parts[0] == 'tests' and any(p in APIS for p in parts[1:-1]):
        return True
    return rel == 'app/backend_choice.h'

# The British forms the tree spells the American way, matched against each part of a name (ratePerMetre's
# Metre) and each word of prose: a word with its endings, or a stem with its -ise, -yse or -lled endings.
BRITISH = re.compile(r'''(?x) (?:un|re|de|kilo|centi|milli|non|pre|sub|over|under|epi)*(?:
    (?:colour|behaviour|neighbour|favour|honour|armour|labour|flavour|harbour|rumour|vapour)(?:s|ed|ing|hood|able)?
  | (?:centre|metre|litre|fibre|theatre)(?:s|d)? | centring
  | grey(?:s|ed|ing|ish|scale)?
  | (?:travel|cancel|model|label|level|signal|fuel|tunnel|bevel|channel|marshal|total|dial|quarrel)l(?:ed|ing|er|ers)
  | (?:organ|real|recogn|initial|normal|serial|optim|custom|minim|maxim|priorit|synchron|visual|summar|emphas|quant
      |raster|linear|memo|final|special|parameter|amort|parallel|orthogonal|relativ|categor|character|author
      |capital|general|standard|stabil|token|random|vector|local|penal|discret|equal|sanit|util|material|apolog)
      is(?:e|es|ed|ing|er|ers|ation|ations)
  | analys(?:e|es|ed|ing|er)
  | (?:licence|defence|offence|programme|artefact|aluminium|whilst|amongst|spelt|analogue|catalogue|judgement|tyre
      |kerb|pavement)s?
)''')
NAME_PART = re.compile(r'[A-Z]+(?=[A-Z][a-z])|[A-Z]?[a-z]+|[A-Z]+')
# Names as their authors published them (examples/the_lab/assets/models/old_tyre,
# examples/the_lab/assets/textures/brick_pavement_02_diff.jpg).
AUTHORS_NAMES = ('old_tyre', 'brick_pavement_02')
# What the spelling check reads: OURS but the third-party code vendored under modules/, a project's models,
# and this file, which lists the words.
NOT_OURS = ('modules/bc7e/',)


def british_words(line):
    """The British forms in @p line, each as it is written."""
    for name in AUTHORS_NAMES:
        line = line.replace(name, '')
    for token in re.findall(r'[A-Za-z]+', line):
        for part in NAME_PART.findall(token):
            if BRITISH.fullmatch(part.lower()):
                yield part


def spelled_files():
    this = Path(__file__).resolve()
    for f in OURS:
        parts = f.split('/')
        models = len(parts) > 4 and parts[0] == 'examples' and parts[2:4] == ['assets', 'models']
        if f.startswith(NOT_OURS) or models or (ROOT / f).resolve() == this:
            continue
        try:
            yield Path(f), (ROOT / f).read_text(encoding='utf-8')
        except (UnicodeDecodeError, OSError):
            continue


def text(path):
    return path.read_text(encoding='utf-8')


def prose(page):
    """A page's text with its fenced code blocks blanked, line numbers kept."""
    lines = text(page).split('\n')
    fenced = False
    for i, line in enumerate(lines):
        if line.lstrip().startswith('```'):
            fenced = not fenced
            lines[i] = ''
        elif fenced:
            lines[i] = ''
    return lines


def slug(heading):
    """The anchor a heading gets on the pages' host."""
    heading = re.sub(r'[`*_]|\[([^\]]*)\]\([^)]*\)', r'\1', heading.strip().lower())
    return re.sub(r'\s', '-', re.sub(r'[^\w\- ]', '', heading))


def anchors(page):
    found = set()
    for line in prose(page):
        if line.startswith('#'):
            found.add(slug(line.lstrip('#')))
    return found


def links(page):
    """Every relative link on @p page: (line number, target file or None, anchor or None)."""
    for number, line in enumerate(prose(page), 1):
        for target in re.findall(r'\]\(([^)\s]+)\)', re.sub(r'`[^`]*`', '', line)):
            if re.match(r'[a-z]+:', target):
                continue
            path, _, anchor = target.partition('#')
            yield number, (page.parent / path).resolve() if path else page, anchor or None


def code_names(page):
    """Every backticked span outside code blocks, with its line number."""
    for number, line in enumerate(prose(page), 1):
        for span in re.findall(r'`([^`]+)`', line):
            yield number, span


def source_files(*folders):
    # A library under modules/ is a repository of its own, so its files are read from the disk.
    for folder in folders:
        if folder.startswith('modules/'):
            found = (p for p in sorted((ROOT / folder).rglob('*')) if p.is_file())
        else:
            found = (ROOT / f for f in OURS if f.startswith(folder + '/'))
        for path in found:
            if path.suffix in SOURCE_EXTENSIONS:
                yield path


def code_lines(path):
    """Lines that are code: not blank, not a comment."""
    count, block = 0, False
    for line in text(path).split('\n'):
        stripped = line.strip()
        if block:
            block = '*/' not in stripped
            continue
        if not stripped or stripped.startswith('//'):
            continue
        if stripped.startswith('/*'):
            block = '*/' not in stripped
            continue
        count += 1
    return count


def includes(path):
    return re.findall(r'^\s*#\s*include\s*"([^"]+)"', text(path), re.M)


def layer_order():
    """architecture.md's engine chain, its core files, and system/'s inner order."""
    page = text(DOCS / 'reference' / 'architecture.md')
    section = page.index('### The layer order')
    fence = page.index('```\n', section) + 4
    chain = page[fence:page.index('\n', fence)]
    ranks = {}
    for rank, step in enumerate(s.strip() for s in chain.split('<')):
        name = step.split(' (')[0].strip()
        ranks.setdefault(step if name == 'core' else name, rank)
    contract = re.search(r'Its contract \(([^)]*)\)', page[section:]).group(1)
    engine = re.search(r'the Engine\s+\(([^)]*)\)', page[section:]).group(1)
    system = re.search(r'Inside `system/` the order is `([^`]+)`', page[section:]).group(1)
    system_ranks = {}
    for rank, step in enumerate(system.split('<')):
        for name in step.split(','):
            system_ranks[name.strip()] = rank
    core_ranks = {}
    for step, rank in ranks.items():
        if step.startswith('core ('):
            core_ranks['engine' if 'Engine' in step else 'contract'] = rank
    return ranks, core_ranks, re.findall(r'`([a-z_]+)', engine), system_ranks


class TheDocsAndTheTreeAgree(unittest.TestCase):
    def test_every_link_and_anchor_resolves(self):
        broken = []
        for page in PAGES:
            for number, target, anchor in links(page):
                where = f'{page.relative_to(ROOT)}:{number}'
                if not target.exists():
                    broken.append(f'{where}: no file {target}')
                elif anchor and target.suffix == '.md' and slug(anchor) not in anchors(target):
                    broken.append(f'{where}: no heading #{anchor} in {target.relative_to(ROOT)}')
        self.assertEqual(broken, [])

    def test_every_page_is_reached_from_the_index(self):
        reached, todo = set(), [DOCS / 'README.md']
        while todo:
            page = todo.pop()
            if page in reached or not page.exists() or page.suffix != '.md':
                continue
            reached.add(page)
            todo.extend(target for _, target, _ in links(page))
        self.assertEqual([str(p.relative_to(ROOT)) for p in PAGES if DOCS in p.parents and p.resolve() not in reached], [])

    def test_every_path_a_page_names_is_in_the_tree(self):
        # A source file by its whole path or the end of one (pages name a header by its file alone,
        # or from its folder down), and anything named from a folder of the repository. A file a
        # host writes - graphics.json, a package's report - is no file of the tree's.
        tree = OURS + [p.relative_to(ROOT).as_posix() for p in (ROOT / 'modules').rglob('*')
                       if p.is_file() and '/.git/' not in p.as_posix()]
        # assets/ is a project's folder as well as the engine's, and a page names a project's.
        tops = tuple(sorted({f.split('/')[0] + '/' for f in OURS if '/' in f} - {'assets/'}))
        missing = []
        for page in PAGES:
            for number, span in code_names(page):
                span = re.sub(r'\.h/\.cpp$', '.h', span)
                if not re.fullmatch(r'[\w./-]+\.\w+', span):
                    continue
                if not (span.startswith(tops) or span.endswith(SOURCE_EXTENSIONS)):
                    continue
                if not any(f == span or f.endswith('/' + span) for f in tree):
                    missing.append(f'{page.relative_to(ROOT)}:{number}: {span}')
        self.maxDiff = None
        self.assertEqual(missing, [])

    def test_every_path_a_comment_names_is_in_the_tree(self):
        # A comment naming a file sends a reader there; one the tree moved sends them nowhere. Checked: a path
        # from a top folder of the tree (shaders/opengl/include/waves.glsl, tests/fixture/), and a source file
        # by the end of its path (render/opengl/gpu.h). A placeholder (tests/<area>/<topic>.cpp) is no path, nor a
        # bare file name, nor a project's own (assets/models/). Under modules/, the engine's own build file is
        # read, not what it vendors.
        files = [f for f in OURS if not f.startswith('modules/') or f == 'modules/CMakeLists.txt']
        known = set(files)
        for f in files:
            parts = f.split('/')
            known.update('/'.join(parts[:i]) for i in range(1, len(parts)))
        tops = '|'.join(sorted({f.split('/')[0] for f in OURS if '/' in f and not f.startswith('.')}
                               - {'modules', 'assets'}))
        from_top = re.compile(r'(?<![\w./<-])((?:' + tops + r')/[\w./-]*[\w/])(?![\w<])')
        source = re.compile(r'(?<![\w./<-])([\w-]+(?:/[\w.-]+)+\.(?:h|cpp|glsl|shader))\b(?![<])')
        comment = re.compile(r'//|/\*|^\s*\*|#(?!\s*(?:include|define|if|endif|else|pragma|undef))')
        missing = []
        for f in files:
            if not f.endswith(('.h', '.cpp', '.glsl', '.shader', '.cmake', 'CMakeLists.txt', '.py')):
                continue
            for number, line in enumerate(text(ROOT / f).split('\n'), 1):
                m = comment.search(line)
                if not m:
                    continue
                said = line[m.start():]
                for path in from_top.findall(said):
                    path = path.rstrip('/')
                    if path not in known and not any(k.endswith('/' + path) for k in known):
                        missing.append(f'{f}:{number}: {path}')
                for path in source.findall(said):
                    if path not in known and not any(k.endswith('/' + path) for k in known):
                        missing.append(f'{f}:{number}: {path}')
        self.maxDiff = None
        self.assertEqual(missing, [])

    def test_every_qualified_name_a_page_gives_is_declared(self):
        tree = '\n'.join(text(p) for p in source_files('src', 'app', 'examples', 'templates', 'modules/vkmLog', 'modules/vkmPlatform', 'modules/vkmAudio', 'modules/vkmNetwork', 'modules/vkmPhysics', 'modules/vkmOpenGL'))
        scopes = set(re.findall(r'\b(?:class|struct|enum class|enum)\s+(\w+)', tree))
        for nested in re.findall(r'namespace\s+([\w:]+)', tree):
            scopes |= set(nested.split('::'))
        words = set(re.findall(r'\b\w+\b', tree))
        unknown = []
        for page in PAGES:
            for number, span in code_names(page):
                name = re.sub(r'\(.*$|<.*$', '', span)
                if not re.fullmatch(r'[A-Z]\w*(::\w+)+', name):
                    continue
                parts = name.split('::')
                if parts[-2] not in scopes or parts[-1] not in words:
                    unknown.append(f'{page.relative_to(ROOT)}:{number}: {span}')
        self.maxDiff = None
        self.assertEqual(unknown, [])

    def test_the_tree_spells_the_american_way(self):
        # Code, comments, strings, shaders, docs, tests, examples, templates and the names a scene saves.
        found = []
        for rel, source in spelled_files():
            for number, line in enumerate(source.split('\n'), 1):
                found.extend(f'{rel.as_posix()}:{number}: {word}' for word in british_words(line))
        self.maxDiff = None
        self.assertEqual(found, [])

    def test_the_docs_name_two_platforms(self):
        found = []
        for page in PAGES:
            for number, line in enumerate(text(page).split('\n'), 1):
                # decisions.md states the rule, so it names what it rules out.
                if page.name != 'decisions.md' and re.search(r'mac\s?os|os\s?x\b', line, re.I):
                    found.append(f'{page.relative_to(ROOT)}:{number}: names macOS')
        self.assertEqual(found, [])

    def test_every_third_party_module_is_justified_in_building(self):
        page = text(DOCS / 'reference' / 'building.md')
        listed = set(re.findall(r'^\| \*\*\w+\*\* \| `modules/(\w+)` \|', page, re.M))
        present = set(MODULES)
        self.assertEqual(sorted(present - listed), [], 'modules no row of building.md justifies')
        self.assertEqual(sorted(listed - present), [], 'rows of building.md naming no module')

    def test_every_library_of_the_engines_holds_its_license_byte_for_byte(self):
        # The build ships the engine's LICENSE alone as licenses/vkmEngine.txt (modules/CMakeLists.txt): one
        # file says the terms of every vkm library only while each holds the same bytes.
        engine = (ROOT / 'LICENSE').read_bytes()
        libraries = [ROOT / 'modules' / name for name in MODULES if name.startswith('vkm')]
        self.assertGreater(len(libraries), 5)
        self.assertEqual([p.name for p in libraries if (p / 'LICENSE').read_bytes() != engine], [])

    def test_every_recipe_takes_ten_lines_or_fewer(self):
        long = []
        blocks = re.findall(r'^```\w*\n(.*?)^```', text(DOCS / 'recipes.md'), re.M | re.S)
        self.assertGreater(len(blocks), 3)
        for block in blocks:
            lines = [l for l in block.split('\n') if l.strip() and not l.strip().startswith('//')]
            if len(lines) > 10:
                long.append(block.split('\n')[0])
        self.assertEqual(long, [])

    def test_no_file_outside_a_backend_names_a_graphics_api(self):
        # Comments and strings too: a comment naming GLAssetMirror says the code knows its backend as surely
        # as an include does. A backend's places are src/backend/<api>/, shaders/<api>/, tests/**/<api>/
        # and app/backend_choice.h; CMake's link lines are the build's half of the hosts' choice.
        found = []
        for p in source_files('src', 'app', 'tests', 'examples', 'templates'):
            rel = p.relative_to(ROOT).as_posix()
            if in_a_backends_place(rel) or '/build/' in rel:
                continue
            for number, line in enumerate(text(p).split('\n'), 1):
                named = GRAPHICS_API.search(BACKEND_CITATION.sub('', line))
                if named:
                    found.append(f'{rel}:{number}: {named.group(0)}')
        self.maxDiff = None
        self.assertEqual(found, [])

    def test_no_call_takes_two_random_draws_in_its_arguments(self):
        # C++ leaves the order of a call's arguments to the compiler, and GCC's is not Clang's: two draws in
        # one call's arguments give one seed two worlds, one per build (a body's spawn point, a particle's).
        # Each draw is a statement of its own, in the order written.
        files = [p for p in source_files('src', 'app', 'examples', 'templates', 'tests')
                 if '/build/' not in p.as_posix()]
        sources = {p: code_only(text(p)) for p in files}
        wraps, given = drawing_functions(sources)
        found = []
        for p in files:
            for line, count in calls_drawing_twice(text(p), wraps, given):
                found.append(f'{p.relative_to(ROOT)}:{line}: {count} draws in one call')
        self.assertIn('frand', wraps)
        self.assertIn('boxPoint', given)
        self.maxDiff = None
        self.assertEqual(found, [])

    def test_imgui_only_in_the_editor(self):
        imgui = re.compile(r'^\s*#\s*include\s*[<"]imgui', re.M)
        found = []
        for p in source_files('src', 'app'):
            in_editor = str(p).startswith((str(ROOT / "src" / "editor"), str(ROOT / "app" / "editor"))) \
                or re.match(r'src/backend/\w+/editor/', p.relative_to(ROOT).as_posix())
            if imgui.search(text(p)) and not in_editor:
                found.append(f'{p.relative_to(ROOT)}: ImGui')
        self.assertEqual(found, [])

    def test_every_reflected_field_is_named_by_its_card(self):
        # A card names each field of its component or the scene's settings by `&Type::field` - a row, a widget
        # of its own or a nested struct - so a field added to a reflect block shows in the Inspector or fails
        # here. A component of the scene format with no card does not link; Name is the header's, not a card's.
        cards = ''.join(text(ROOT / f) for f in OURS
                        if re.fullmatch(r'src/editor/panels/inspector/cards_\w+\.cpp', f))
        blocks = re.compile(r'VKM_REFLECT_BEGIN\(::Vkm::Engine::(\w+)\)(.*?)VKM_REFLECT_END', re.S)
        fields, unnamed = 0, []
        for p in source_files('src/engine/ecs/component', 'src/engine/ecs/settings'):
            for block in blocks.finditer(text(p)):
                kind = block.group(1)
                if kind == 'Name':
                    continue
                for field in re.findall(r'VKM_F\((\w+)', block.group(2)):
                    fields += 1
                    if f'&{kind}::{field}' not in cards:
                        unnamed.append(f'{kind}::{field}')
        self.assertGreater(fields, 200)
        self.maxDiff = None
        self.assertEqual(unnamed, [])

    def test_every_engine_include_keeps_the_layer_order(self):
        ranks, core_ranks, engine_files, system_ranks = layer_order()
        engine = ROOT / 'src' / 'engine'

        def layer(path):
            top = path.split('/')[0]
            if top != 'core':
                return ranks.get(top)
            if path.startswith('core/frame/'):
                return ranks.get('core/frame')
            stem = Path(path).stem
            return core_ranks['engine' if stem in engine_files else 'contract']

        def system_folder(path):
            parts = path.split('/')
            return parts[1] if parts[0] == 'system' and len(parts) > 2 else None

        offenders = []
        for p in source_files('src/engine'):
            source = p.relative_to(engine).as_posix()
            for target in includes(p):
                if not (engine / target).exists():
                    continue
                keeps = layer(source) is not None and layer(target) is not None and layer(target) <= layer(source)
                a, b = system_folder(source), system_folder(target)
                if a and b and a != b:
                    keeps = a in system_ranks and b in system_ranks and system_ranks[b] < system_ranks[a]
                if not keeps:
                    offenders.append(f'{source} -> {target}')
        self.assertGreater(len(ranks), 5)
        self.assertEqual(offenders, [])

    def test_every_backend_include_keeps_the_layer_order(self):
        # architecture.md's chain for a backend, folders of one step peers; its top level's shared files are
        # anyone's to include. A backend's editor/ is its own target, above the rest.
        page = text(DOCS / 'reference' / 'architecture.md')
        found = re.search(r"The backend: `([^`]+)`; the top\s+level's four shared files \(([^)]*)\)", page)
        ranks = {}
        for rank, step in enumerate(found.group(1).split('<')):
            for name in step.strip().strip('{}').split(','):
                ranks['' if 'top level' in name else name.strip()] = rank
        shared = {f'{name}.h' for name in re.findall(r'`(\w+)`', found.group(2))}
        self.assertEqual(len(shared), 4)

        offenders = []
        for api in APIS:
            backend = ROOT / 'src' / 'backend' / api
            for p in source_files(f'src/backend/{api}'):
                source = p.relative_to(backend).as_posix()
                if source.startswith('editor/'):
                    continue
                for target in includes(p):
                    if not (backend / target).exists() or target in shared:
                        continue
                    a, b = (path.split('/')[0] if '/' in path else '' for path in (source, target))
                    if a not in ranks or b not in ranks or ranks[b] > ranks[a]:
                        offenders.append(f'{source} -> {target}')
        self.assertEqual(offenders, [])

    def test_no_engine_file_names_glfw_or_the_systems_headers(self):
        # decisions.md: the Windows headers and GLFW are vkmPlatform's and Winsock is vkmNetwork's; the engine
        # asks through them. The editor and a backend reach GLFW themselves.
        named = re.compile(r'glfw|windows(?:_api)?\.h|winsock', re.I)
        found = []
        for p in source_files('src/engine'):
            for number, line in enumerate(text(p).split('\n'), 1):
                if named.search(line):
                    found.append(f'{p.relative_to(ROOT)}:{number}')
        self.assertEqual(found, [])


def report_the_code_size():
    """Each area's code lines, and every file of src/ or app/ past 1,500 lines, for whoever reads the run:
    targets.md sets no limit, so nothing here fails."""
    long_file = 1500  # lines; past it a file usually holds more than one job
    areas, long = {}, []
    for path in source_files('src', 'app'):
        rel = path.relative_to(ROOT).parts
        area = '/'.join(rel[:2]) if rel[0] == 'src' else rel[0]
        areas[area] = areas.get(area, 0) + code_lines(path)
        lines = text(path).count('\n')
        if lines > long_file:
            long.append(f'{path.relative_to(ROOT)} is {lines} lines')
    print('How much code each area holds:')
    for area, lines in sorted(areas.items()):
        print(f'    {area}: {lines} code lines')
    for line in long:
        print(f'    {line}')


if __name__ == '__main__':
    report_the_code_size()
    unittest.main(verbosity=2)
