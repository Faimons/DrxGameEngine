// Prepares raw prop models (Assets/labor) for the SHIFT map editor:
// real-world size in metres, pivot at the bottom centre, kits split into single parts,
// heavy meshes simplified and textures downsized. Usage: npm run props [-- --only <source>]

import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { Logger, NodeIO } from '@gltf-transform/core';
import { ALL_EXTENSIONS } from '@gltf-transform/extensions';
import { dedup, flatten, getBounds, join, metalRough, normals, prune, simplify, weld } from '@gltf-transform/functions';
import { MeshoptSimplifier } from 'meshoptimizer';
import sharp from 'sharp';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const SOURCE_DIR = path.join(ROOT, 'Assets/labor');
const TARGET_DIR = path.join(ROOT, 'Games/Shift/Assets/Models/Props');

const MAX_TRIANGLES = 60000;
const TEXTURE_SIZE = 1024;
const LARGE_TEXTURE_SIZE = 2048;

// source: file in Assets/labor, out: <category>/<name>, height: target height in metres (null keeps the size),
// scale: size factor when no height is given, split: every top level part becomes its own prop,
// names: file names for split parts, drop: top level parts to remove (ground planes), large: keep bigger textures
const CATALOG = [
    { source: 'pipe_straight.glb', out: 'Rohre/rohr_gerade', height: null },
    { source: 'double_pipe_straight.glb', out: 'Rohre/doppelrohr_gerade', height: null },
    { source: 'industrial_pipe_construction_kit_psx_style.glb', out: 'Rohre/Industrie', height: null, split: true },
    { source: 'large_modular_pipes_rust.glb', out: 'Rohre/Rost', height: null, scale: 0.25, split: true },
    { source: 'small_modular_pipes_copper.glb', out: 'Rohre/Kupfer', height: null, scale: 0.05, split: true },

    { source: 'ventilation_duct.glb', out: 'Lueftung/lueftungskanal', height: null },
    { source: 'modular_ventilation_duct_kit_free.glb', out: 'Lueftung/Kanalsystem', height: null, split: true },
    { source: 'outside_conditioner_block_lowpoly.glb', out: 'Lueftung/klimageraet', height: 1.0 },

    { source: 'hemical_reactor.glb', out: 'Maschinen/chemiereaktor', height: 4.0, drop: ['Cylinder.116'], large: true },
    { source: 'reactor.glb', out: 'Maschinen/reaktor_klein', height: null },
    { source: 'reactor_nuclear.glb', out: 'Maschinen/kernreaktor', height: 5.0, large: true },
    { source: 'laboratory_machine.glb', out: 'Maschinen/labormaschine', height: null },
    { source: 'retro_industrial_control_cabinet.glb', out: 'Maschinen/schaltschrank', height: null },

    { source: 'chemistry_glassware.glb', out: 'Einrichtung/laborglas', height: 0.3 },
    { source: 'cyberdeck_computer_rpi_dev.glb', out: 'Einrichtung/cyberdeck', height: null },
    { source: 'laboratory__hospital_troly.glb', out: 'Einrichtung/rollwagen', height: null, drop: ['Plane'] },
    { source: 'wheel_chair.glb', out: 'Einrichtung/rollstuhl', height: 0.95 },
    { source: 'office_decor_organiser_paper.glb', out: 'Einrichtung/ablage', height: null },
    { source: 'whiteboard.glb', out: 'Einrichtung/whiteboard', height: 1.9 },
    { source: 'whiteboard (1).glb', out: 'Einrichtung/whiteboard_staender', height: 1.9 },
    { source: 'whiteboard (2).glb', out: 'Einrichtung/whiteboard_wand', height: 1.06 },

    { source: 'door.glb', out: 'Tueren/tuer', height: null },
    { source: 'laboratory_door.glb', out: 'Tueren/labortuer', height: 2.1 },

    { source: 'steel_beam_a.glb', out: 'Bau/stahltraeger', height: null },
    { source: 'road_block.glb', out: 'Bau/absperrung', height: 1.0 },
    { source: 'warning_signs__virus_laboratory.glb', out: 'Bau/Warnschilder', height: null, split: true, names: ['schild_a', 'schild_b'] },

    { source: 'laboratory.glb', out: 'Raeume/labor', height: null, large: true },
];

const io = new NodeIO().setLogger(new Logger(Logger.Verbosity.WARN)).registerExtensions(ALL_EXTENSIONS).registerDependencies({ 'meshopt.simplifier': MeshoptSimplifier });

function slug(text) {
    return text.normalize('NFKD').replace(/[̀-ͯ]/g, '').toLowerCase()
        .replace(/[^a-z0-9]+/g, '_').replace(/^_+|_+$/g, '') || 'teil';
}

function countTriangles(document) {
    let triangles = 0;
    for (const mesh of document.getRoot().listMeshes()) {
        for (const primitive of mesh.listPrimitives()) {
            const indices = primitive.getIndices();
            const positions = primitive.getAttribute('POSITION');
            triangles += Math.floor((indices ? indices.getCount() : positions ? positions.getCount() : 0) / 3);
        }
    }
    return triangles;
}

// The visible parts of a model: skip single-child wrapper nodes exported by Sketchfab and Blender
function topLevelParts(scene) {
    let parts = scene.listChildren();
    while (parts.length === 1 && parts[0].listChildren().length > 0 && !parts[0].getMesh()) {
        parts = parts[0].listChildren();
    }
    return parts;
}

function removeParts(document, remove) {
    const scene = document.getRoot().getDefaultScene() ?? document.getRoot().listScenes()[0];
    topLevelParts(scene).forEach((part, i) => {
        if (remove(part, i)) part.dispose();
    });
}

// Scale to the target height and move the bottom centre to the origin under one new root node
function normalize(document, height, factor = 1) {
    const scene = document.getRoot().getDefaultScene() ?? document.getRoot().listScenes()[0];
    const { min, max } = getBounds(scene);
    const size = [max[0] - min[0], max[1] - min[1], max[2] - min[2]];
    if (size.some((v) => !Number.isFinite(v)) || size[1] < 0) throw new Error('model has no geometry');

    const scale = height && size[1] > 1e-6 ? height / size[1] : factor;
    const root = document.createNode('prop_root')
        .setScale([scale, scale, scale])
        .setTranslation([-(min[0] + max[0]) * 0.5 * scale, -min[1] * scale, -(min[2] + max[2]) * 0.5 * scale]);
    for (const child of scene.listChildren()) {
        scene.removeChild(child);
        root.addChild(child);
    }
    scene.addChild(root);
    return size.map((v) => v * scale);
}

async function optimize(document, large) {
    const root = document.getRoot();
    for (const animation of root.listAnimations()) animation.dispose();
    if (root.listExtensionsUsed().some((e) => e.extensionName === 'KHR_materials_pbrSpecularGlossiness')) {
        await document.transform(metalRough());
    }
    await document.transform(dedup(), flatten(), join());

    // Hard-edged normals split every vertex and block simplification: drop them, simplify, rebuild smooth ones
    if (countTriangles(document) > MAX_TRIANGLES) {
        for (const mesh of root.listMeshes()) {
            for (const primitive of mesh.listPrimitives()) {
                primitive.setAttribute('NORMAL', null).setAttribute('TANGENT', null);
            }
        }
        await document.transform(weld());
        for (let error = 0.001; countTriangles(document) > MAX_TRIANGLES && error <= 0.2; error *= 2) {
            const ratio = MAX_TRIANGLES / countTriangles(document);
            await document.transform(simplify({ simplifier: MeshoptSimplifier, ratio, error }));
        }
        await document.transform(normals({ overwrite: true }));
    }
    await document.transform(weld(), prune());
    await shrinkTextures(document, large ? LARGE_TEXTURE_SIZE : TEXTURE_SIZE);
}

// Downsize every texture; images without transparency become JPEG
async function shrinkTextures(document, size) {
    for (const texture of document.getRoot().listTextures()) {
        const image = texture.getImage();
        if (!image) continue;
        const input = sharp(Buffer.from(image));
        const { isOpaque } = await input.stats();
        const resized = input.resize(size, size, { fit: 'inside', withoutEnlargement: true });
        const output = isOpaque ? await resized.jpeg({ quality: 85, mozjpeg: true }).toBuffer()
                                : await resized.png({ compressionLevel: 9 }).toBuffer();
        texture.setImage(new Uint8Array(output)).setMimeType(isOpaque ? 'image/jpeg' : 'image/png')
            .setURI((texture.getURI() || texture.getName() || 'texture').replace(/\.[^.]+$/, '') + (isOpaque ? '.jpg' : '.png'));
    }
}

async function writeProp(document, entry, file) {
    await optimize(document, entry.large);
    const size = normalize(document, entry.height, entry.scale);
    await fs.mkdir(path.dirname(file), { recursive: true });
    await io.write(file, document);
    const { size: bytes } = await fs.stat(file);
    console.log(`  ${path.relative(TARGET_DIR, file).padEnd(44)} ${String(countTriangles(document)).padStart(6)} tris  ` +
        `${(bytes / 1e6).toFixed(1).padStart(5)} MB  ${size.map((v) => v.toFixed(2)).join(' x ')} m`);
}

async function processEntry(entry) {
    const source = path.join(SOURCE_DIR, entry.source);
    try {
        await fs.access(source);
    } catch {
        console.warn(`fehlt: ${entry.source}`);
        return 0;
    }
    console.log(entry.source);

    const read = async () => {
        const document = await io.read(source);
        if (entry.drop) removeParts(document, (part) => entry.drop.includes(part.getName()));
        return document;
    };

    if (!entry.split) {
        await writeProp(await read(), entry, path.join(TARGET_DIR, `${entry.out}.glb`));
        return 1;
    }

    // Kits: one file per top level part, names from the node names
    const probe = await read();
    const names = topLevelParts(probe.getRoot().getDefaultScene() ?? probe.getRoot().listScenes()[0])
        .map((node, i) => entry.names?.[i] ?? slug(node.getName() || `teil_${i + 1}`));
    await fs.rm(path.join(TARGET_DIR, entry.out), { recursive: true, force: true });
    const used = new Map();
    for (let i = 0; i < names.length; ++i) {
        const count = (used.get(names[i]) ?? 0) + 1;
        used.set(names[i], count);
        const name = count > 1 ? `${names[i]}_${count}` : names[i];

        const document = await read();
        removeParts(document, (part, index) => index !== i);
        await writeProp(document, entry, path.join(TARGET_DIR, entry.out, `${name}.glb`));
    }
    return names.length;
}

async function main() {
    const only = process.argv.includes('--only') ? process.argv[process.argv.indexOf('--only') + 1] : null;
    const entries = only ? CATALOG.filter((e) => e.source === only) : CATALOG;

    const known = new Set(CATALOG.map((e) => e.source));
    for (const file of await fs.readdir(SOURCE_DIR).catch(() => [])) {
        if (/\.(glb|gltf)$/i.test(file) && !known.has(file)) console.warn(`nicht im Katalog: ${file}`);
    }

    let written = 0;
    for (const entry of entries) {
        try {
            written += await processEntry(entry);
        } catch (error) {
            console.error(`FEHLER ${entry.source}: ${error.message}`);
            process.exitCode = 1;
        }
    }
    console.log(`${written} Deko-Modelle in ${path.relative(ROOT, TARGET_DIR)}`);
}

await main();
