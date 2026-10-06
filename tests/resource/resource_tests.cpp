#include "support.h"

#include <cmath>
#include <initializer_list>

#include <nlohmann/json.hpp>

#include "debug/engine_error_log.h"
#include "resource/asset/font_asset.h"
#include "resource/asset_source_kind.h"
#include "resource/texture_format.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/texture_asset.h"
#include "resource/generate/mesh_generators.h"
#include "resource/generate/texture_generators.h"

namespace {

// Assets are owned once, referenced by a generational handle, and serialized by
// name - listed in `docs/guides/engine.md` among what everything stands on.

void testHandlesAndNames() {
    std::printf("What a handle and a name each promise:\n");

    ResourceManager resources;

    const MeshHandle cube = resources.add(generateCube(), "test:cube");
    check("adding an asset yields a live handle", bool(cube) && resources.isAlive(cube));
    check("and it kept the name it was given", resources.get(cube).name() == "test:cube");

    check("a name resolves back to the same handle", resources.findByName<MeshAsset>("test:cube") == cube);
    check("and a name nobody took resolves to nothing", !resources.findByName<MeshAsset>("test:absent"));

    // The name is the identity that crosses a save; two assets on one name would
    // make a scene file ambiguous.
    const MeshHandle second = resources.add(generateSphere(), "test:sphere");
    check("a second asset is its own handle", second != cube);
    check("and does not answer to the first's name", resources.findByName<MeshAsset>("test:cube") == cube);

    resources.rename(second, "test:renamed");
    check("a rename moves the name", !resources.findByName<MeshAsset>("test:sphere"));
    check("  to the new one", resources.findByName<MeshAsset>("test:renamed") == second);
    check("  and the handle is unchanged by it", resources.isAlive(second));
}

void testAHandleOutlivingItsAsset() {
    std::printf("A handle held past the asset under it:\n");

    ResourceManager resources;
    const MeshHandle mesh = resources.add(generateCube(), "test:doomed");
    check("it starts alive", resources.isAlive(mesh));

    resources.remove(mesh);
    check("and is not alive once removed", !resources.isAlive(mesh));
    check("and its name is free again", !resources.findByName<MeshAsset>("test:doomed"));

    // A new asset in the freed slot must not answer to the old handle, or a stale
    // reference silently reads whatever replaced it.
    const MeshHandle reused = resources.add(generateSphere(), "test:doomed");
    check("a new asset may take the freed name", bool(reused));
    check("but the old handle is still dead", !resources.isAlive(mesh));
    check("and the two handles are not equal", !(reused == mesh));
}

void testTypesDoNotShareASpace() {
    std::printf("Two asset kinds under one manager:\n");

    ResourceManager resources;
    const MeshHandle    mesh    = resources.add(generateCube(), "shared:name");
    const TextureHandle white = generateWhiteTexture(resources);
    resources.rename(white, "shared:name");
    const TextureHandle texture = white;

    // Names are per-kind: the scene writes the kind beside the name, so a mesh and
    // a texture sharing one do not collide.
    check("both exist", resources.isAlive(mesh) && resources.isAlive(texture));
    check("the mesh name finds the mesh", resources.findByName<MeshAsset>("shared:name") == mesh);
    check("the texture name finds the texture", resources.findByName<TextureAsset>("shared:name") == texture);

    resources.remove(mesh);
    check("removing one leaves the other", resources.isAlive(texture));
    check("  and its name still resolves", resources.findByName<TextureAsset>("shared:name") == texture);
}

void testEditingInPlace() {
    std::printf("Editing an asset through its handle:\n");

    ResourceManager resources;
    const MeshHandle mesh = resources.add(generateCube(), "test:edited");
    const size_t before = resources.get(mesh).vertices.size();

    resources.edit(mesh).vertices.clear();
    check("edit reaches the same asset get sees", resources.get(mesh).vertices.empty());
    check("  and it was not empty to begin with", before > 0);
    check("  and the handle still names it", resources.isAlive(mesh));
}

void testAddingUnderATakenNameReplacesIt() {
    std::printf("Adding a second asset under a name already taken:\n");

    ResourceManager resources;
    const MeshHandle rig = resources.add(generateCube(), "test:rig");
    const uint64_t   uid = resources.get(rig).uid();

    // A script reload reruns onStart, building the asset again under the same name.
    // Suffixing it to "test:rig (2)" would split the scene; the name must keep
    // meaning one asset.
    const MeshHandle again = resources.add(generateSphere(), "test:rig");
    check("the name hands back the handle that already had it", again == rig);
    check(
        "  which is still the one the name resolves to",
        resources.findByName<MeshAsset>("test:rig") == rig
    );
    check("  and no second asset was made", !resources.findByName<MeshAsset>("test:rig (2)"));

    // Replaced, not ignored: the caller built a new asset and meant it.
    check(
        "the contents are the ones just added",
        resources.get(rig).vertices.size() == MeshAsset(generateSphere()).vertices.size()
    );
    check("  the uid says a different asset sits here", resources.get(rig).uid() != uid);
    check("  and the version moved, so a GPU cache re-uploads", resources.get(rig).version() > 1);

    // An unnamed asset claims no identity, so two are two assets.
    const MeshHandle anon  = resources.add(generateCube());
    const MeshHandle anon2 = resources.add(generateCube());
    check("two unnamed assets stay two assets", anon != anon2 && resources.isAlive(anon));

    // An asset re-declared with its own contents: the identity is read off the slot
    // before the incoming is built, or the move leaves it nameless under its name.
    const MeshHandle self  = resources.add(generateCube(), "test:self");
    const MeshHandle again2 = resources.add(std::move(resources.edit(self)), "test:self");
    check("re-declaring an asset with its own contents keeps its handle", again2 == self);
    check("  it still knows its name", resources.get(self).name() == "test:self");
    check("  which still resolves to it", resources.findByName<MeshAsset>("test:self") == self);
    check("  and it still holds contents", !resources.get(self).vertices.empty());
}

// Every generator must stamp its recipe: without one the cooker skips the mesh with
// a warning and a scene naming it does not load. And the recipe must rebuild the
// same mesh, or it is the same failure one load later.
void testEveryGeneratedMeshCarriesItsRecipe() {
    std::printf("What a generated mesh says about how to remake it:\n");

    struct Made {
        const char* type;
        MeshAsset mesh;
    };
    std::vector<Made> made;
    made.push_back({"triangle", generateTriangle(2.0f)});
    made.push_back({"plane",    generatePlane(2.0f, 3.0f, 2, 3)});
    made.push_back({"cube",     generateCube()});
    made.push_back({"sphere",   generateSphere(12, 6)});
    made.push_back({"pyramid",  generatePyramid(2.0f, 0.5f)});
    made.push_back({"cone",     generateCone(0.25f, 2.0f, 7)});
    made.push_back({"cylinder", generateCylinder(0.25f, 2.0f, 9)});

    ResourceManager resources;
    std::vector<std::string> unstamped;
    std::vector<std::string> mislabelled;
    std::vector<std::string> unbuildable;
    for (const Made& one : made) {
        if (!one.mesh.hasSource()) {
            unstamped.emplace_back(one.type);
            continue;
        }
        const nlohmann::json& recipe = one.mesh.sourceJson();
        if (recipe.value("kind", std::string{}) != "generator"
            || recipe.value("type", std::string{}) != one.type) {
            mislabelled.emplace_back(one.type);
        }
        const MeshHandle rebuilt = createGeneratedMesh(recipe, resources);
        const bool same = rebuilt
            && resources.get(rebuilt).vertices.size() == one.mesh.vertices.size()
            && resources.get(rebuilt).indices == one.mesh.indices
            && resources.get(rebuilt).boundsMax == one.mesh.boundsMax;
        if (!same) unbuildable.emplace_back(one.type);
    }
    for (const std::string& one : unstamped)   std::printf("      %s carries none\n", one.c_str());
    for (const std::string& one : mislabelled) std::printf("      %s is mislabelled\n", one.c_str());
    for (const std::string& one : unbuildable) {
        std::printf("      its recipe does not rebuild the %s it came from\n", one.c_str());
    }

    check("every generator stamps a recipe", unstamped.empty());
    check("  naming the generator that made it", mislabelled.empty());
    check("  which rebuilds that same mesh, parameters and all", unbuildable.empty());
}

// Generated textures are read back by the same file; a solid's usage is half of
// what it is.
void testEveryGeneratedTextureRebuildsFromItsRecipe() {
    std::printf("What a generated texture says about how to remake it:\n");

    ResourceManager made;
    ResourceManager rebuilt;
    std::vector<std::string> wrong;
    const auto compare = [&](const char* what, TextureHandle original) {
        const TextureAsset& was = made.get(original);
        const TextureHandle again = createGeneratedTexture(was.sourceJson(), rebuilt);
        if (!again || rebuilt.get(again).pixelData != was.pixelData
            || rebuilt.get(again).usage() != was.usage()) {
            wrong.emplace_back(what);
        }
    };
    compare("white",  generateWhiteTexture(made));
    compare("black",  generateBlackTexture(made));
    compare("normal", generateNormalTexture(made));
    compare("gray",   generateGrayTexture(made));
    compare(
        "a solid colour",
        createSolidColorTexture(glm::vec4(0.2f, 0.4f, 0.6f, 0.8f), made, TextureUsage::Color)
    );
    compare(
        "a solid normal",
        createSolidColorTexture(glm::vec4(0.5f, 0.25f, 1.0f, 1.0f), made, TextureUsage::Normal)
    );
    for (const std::string& one : wrong) std::printf("      %s does not come back\n", one.c_str());

    check("every generated texture rebuilds from its recipe, texels and usage", wrong.empty());
}

// Usage decides how texels decode, and a recipe can be misspelt: "Colour" read
// silently as Data is a washed-out colour map with nothing saying why.
void testAMisspeltUsageIsReported() {
    std::printf("A texture recipe's usage:\n");

    EngineErrorLog errors;
    setErrorSink(&errors);
    const TextureUsage spelt  = textureUsageFromRecipe({{"usage", "Color"}});
    const TextureUsage absent = textureUsageFromRecipe({{"path", "assets/rock.png"}});
    const unsigned long long quiet = errors.totalPushed();
    const TextureUsage misspelt =
        textureUsageFromRecipe({{"path", "assets/rock.png"}, {"usage", "Colour"}});
    const TextureUsage mistyped = textureUsageFromRecipe({{"usage", 2}});
    setErrorSink(nullptr);

    check("a usage this build has reads as itself", spelt == TextureUsage::Color);
    check("  and one the recipe lacks as Data, without a word", absent == TextureUsage::Data && quiet == 0);
    check(
        "one it does not have reads as Data",
        misspelt == TextureUsage::Data && mistyped == TextureUsage::Data
    );
    check(
        "  and is reported, naming the file",
        errors.totalPushed() == 2
            && !errors.entries().empty()
            && errors.entries().front().source.find("assets/rock.png") != std::string::npos
    );
}

// The forward shader rebuilds the bitangent as cross(N, T) * w
// (shaders/forward/pbr/fragment.shader), so a generator's tangent must run the way
// U grows and cross(N, T) * w the way V grows, measured off positions and UVs.
// Winding must face the vertex normals, or a triangle is culled from its lit side.
void testGeneratedMeshesFaceAndMapTheWayTheirVerticesSay() {
    std::printf("A generated mesh's triangles against its own vertices:\n");

    struct Made {
        const char* type;
        MeshAsset mesh;
    };
    std::vector<Made> made;
    made.push_back({"triangle", generateTriangle()});
    made.push_back({"plane",    generatePlane()});
    made.push_back({"cube",     generateCube()});
    made.push_back({"sphere",   generateSphere()});
    made.push_back({"pyramid",  generatePyramid()});
    made.push_back({"cone",     generateCone()});
    made.push_back({"cylinder", generateCylinder()});

    for (const Made& one : made) {
        int backwards = 0;
        int offU      = 0;
        int offV      = 0;
        const std::vector<Vertex>&   v = one.mesh.vertices;
        const std::vector<uint32_t>& i = one.mesh.indices;
        for (size_t t = 0; t + 2 < i.size(); t += 3) {
            const Vertex& a = v[i[t]];
            const Vertex& b = v[i[t + 1]];
            const Vertex& c = v[i[t + 2]];
            const glm::vec3 e1 = b.position - a.position;
            const glm::vec3 e2 = c.position - a.position;
            if (glm::dot(glm::cross(e1, e2), a.normal + b.normal + c.normal) <= 0.0f) ++backwards;

            // The directions U and V grow in, solved from the edges; a triangle
            // with no UV area has nothing to compare.
            const glm::vec2 d1  = b.uv - a.uv;
            const glm::vec2 d2  = c.uv - a.uv;
            const float     det = d1.x * d2.y - d2.x * d1.y;
            if (std::abs(det) <= glm::epsilon<float>()) continue;
            const glm::vec3 alongU = (e1 * d2.y - e2 * d1.y) / det;
            const glm::vec3 alongV = (e2 * d1.x - e1 * d2.x) / det;
            for (const Vertex* corner : {&a, &b, &c}) {
                const glm::vec3 tangent(corner->tangent);
                if (glm::dot(tangent, alongU) <= 0.0f) ++offU;
                if (glm::dot(glm::cross(corner->normal, tangent) * corner->tangent.w, alongV) <= 0.0f) ++offV;
            }
        }
        if (backwards || offU || offV) {
            std::printf(
                "      %s: %d triangles wound away from their normals, %d corners with the "
                "tangent off +U, %d with the bitangent off +V\n",
                one.type,
                backwards,
                offU,
                offV
            );
        }
        char label[64];
        std::snprintf(label, sizeof(label), "%s is wound to face its normals", one.type);
        check(label, backwards == 0);
        check("  its tangents run along +U", offU == 0);
        check("  and cross(N, T) * w runs along +V", offV == 0);
    }
}

// Storage format and upload layout are inferred separately from one channel count,
// and GL samples whatever pair it gets: one channel into RGBA is red, grey+alpha as
// RG is red and green with no alpha. So every count, for every usage, must give a
// pair that agrees - and keep its usage, which only the format records once decoded.
void testEveryImageDecodesToFormatsThatAgree() {
    std::printf("What an image file's channel count is stored and uploaded as:\n");

    const auto storedChannels = [](TextureInternalFormat format) {
        switch (format) {
            case TextureInternalFormat::R8:     return 1;
            case TextureInternalFormat::RG8:    return 2;
            case TextureInternalFormat::RGB8:
            case TextureInternalFormat::SRGB8:  return 3;
            default:                            return 4;
        }
    };
    const auto uploadedChannels = [](TexturePixelFormat format) {
        switch (format) {
            case TexturePixelFormat::R:   return 1;
            case TexturePixelFormat::RG:  return 2;
            case TexturePixelFormat::RGB: return 3;
            case TexturePixelFormat::RGBA: break;
        }
        return 4;
    };

    for (const TextureUsage usage : {TextureUsage::Color, TextureUsage::Data, TextureUsage::Normal}) {
        for (int file = 1; file <= 4; ++file) {
            const int decoded = decodeChannels(file, usage);
            const TextureInternalFormat stored = inferInternalFormat(decoded, usage);
            char label[96];
            std::snprintf(
                label,
                sizeof(label),
                "a %d-channel file as %s stores what it uploads",
                file,
                Reflect::enumName(usage)
            );
            check(label, storedChannels(stored) == uploadedChannels(inferFormat(decoded)));
            std::snprintf(label, sizeof(label), "  and reads back as %s", Reflect::enumName(usage));
            check(label, textureUsageOf(stored) == usage);
        }
    }
    check("grey and alpha keeps its alpha", decodeChannels(2, TextureUsage::Data) == 4);
    check("a grey data map stays one channel", decodeChannels(1, TextureUsage::Data) == 1);
    check("a normal keeps its x and y alone", decodeChannels(3, TextureUsage::Normal) == 2);
}

// A font is baked once at startup and never enters a scene file; UIText names it,
// so the name must keep resolving after a load trades the rest of the graph away.
void testFontsStayWhenTheGraphIsTraded() {
    std::printf("What a graph swap or clear leaves behind:\n");

    ResourceManager live;
    live.add(FontAsset{}, "ui:test");
    live.add(generateCube(), "old:cube");

    ResourceManager staged;
    staged.add(generateSphere(), "new:sphere");

    const uint64_t epoch = live.epoch();
    live.swap(staged);
    check("the loaded graph comes in",  bool(live.findByName<MeshAsset>("new:sphere")));
    check(
        "  and the old one goes out",
        !live.findByName<MeshAsset>("old:cube") && bool(staged.findByName<MeshAsset>("old:cube"))
    );
    check(
        "the font stays where it was baked",
        bool(live.findByName<FontAsset>("ui:test")) && !staged.findByName<FontAsset>("ui:test")
    );
    check("  and the epoch says the graph was replaced", live.epoch() != epoch);

    live.clear();
    check("clear drops the graph", !live.findByName<MeshAsset>("new:sphere"));
    check("  and keeps the font, as a swap does", bool(live.findByName<FontAsset>("ui:test")));
}

} // namespace

void runResourceTests() {
    testHandlesAndNames();
    testAHandleOutlivingItsAsset();
    testTypesDoNotShareASpace();
    testEditingInPlace();
    testAddingUnderATakenNameReplacesIt();
    testEveryGeneratedMeshCarriesItsRecipe();
    testEveryGeneratedTextureRebuildsFromItsRecipe();
    testAMisspeltUsageIsReported();
    testGeneratedMeshesFaceAndMapTheWayTheirVerticesSay();
    testEveryImageDecodesToFormatsThatAgree();
    testFontsStayWhenTheGraphIsTraded();
}
