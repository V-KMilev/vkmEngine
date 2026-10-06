flat in uint vObject;

layout(location = 0) out uint object;

void main() {
    // One past the index, so a pixel nothing covered reads zero.
    object = vObject + 1u;
}
