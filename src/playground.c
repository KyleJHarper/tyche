/*
 * playground.c
 *
 *  Created on: Mar 15, 2016
 *      Author: administrator
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "zstd/zstd.h"

int main() {
  char *data = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Curabitur ut orci vehicula, dictum turpis in, posuere ante. Nulla vel convallis orci. Cras feugiat accumsan felis, non aliquam sem facilisis eget. Sed ultricies sem leo, nec maximus massa facilisis vitae. Donec porta venenatis posuere. Phasellus ultrices condimentum sem, non lacinia leo lacinia non. Aenean blandit volutpat varius. Vestibulum at tincidunt nulla. Fusce accumsan nisi sed lectus tempor tristique sit amet nec felis. Praesent nec massa eget augue ornare pharetra. Curabitur sit amet metus a dui porta feugiat.  In et maximus ipsum. In interdum arcu ut dolor molestie, id convallis turpis lobortis. Vestibulum iaculis libero semper orci rhoncus, facilisis mattis diam dictum. Etiam condimentum, nisi rhoncus aliquet lacinia, ligula neque imperdiet nulla, lobortis eleifend risus nulla at dui. Donec rhoncus sit amet tellus ut congue. Pellentesque aliquam lacus mattis magna mattis vehicula. Mauris ante dui, molestie ac pharetra quis, sagittis eget ligula. Aliquam ut neque quis enim sollicitudin laoreet id sit amet justo. Phasellus laoreet leo metus, at ornare metus ullamcorper sit amet. Maecenas in felis sit amet nisl iaculis elementum nec vel nibh. Nulla sed dui sit amet tortor congue tristique sit amet nec mi. Fusce id condimentum eros, eget maximus turpis.  Nunc ornare rhoncus turpis, et euismod nunc scelerisque non. Donec vel finibus nibh. Sed risus ipsum, molestie vel nulla fermentum, mattis mattis ante. Nulla volutpat dignissim arcu, ut pretium nisi interdum ac. In faucibus iaculis fringilla. Duis quam arcu, tempus vitae luctus nec, semper in ipsum. Nullam sit amet semper augue. Curabitur sit amet facilisis ligula, quis varius neque. Aliquam sed pretium odio. Vivamus luctus lacus eget dapibus elementum. Aenean vulputate ante eu dolor dictum ornare.  Quisque metus lacus, elementum a orci vel, lacinia porta arcu. Duis velit erat, rhoncus ultrices ullamcorper vitae, posuere vitae lorem. In scelerisque mi in diam cursus, at congue tortor fringilla. Morbi eu dapibus ante. Praesent porta porta enim, vel varius elit. In rhoncus suscipit enim eu ornare. Morbi tempus, mauris sed malesuada fringilla, quam tellus imperdiet justo, malesuada tincidunt leo elit nec tellus. Nullam eu nibh lacus. Nulla sit amet purus eget turpis imperdiet tincidunt quis at neque. Sed mattis lorem nec lorem condimentum fermentum. Phasellus laoreet arcu vel dui finibus fringilla. Aliquam condimentum odio nec tortor volutpat, sit amet molestie elit ultricies.  Aliquam quis eros arcu. Aenean ultricies odio at venenatis ornare. Fusce convallis mattis lacus, a efficitur augue pharetra vitae. Suspendisse pharetra non turpis vel semper. Curabitur vulputate ligula quam, id finibus massa posuere eu. Quisque diam elit, ultrices nec nisi a, ornare facilisis urna. Phasellus fringilla rutrum orci ac tristique. Vestibulum blandit, ipsum a dictum faucibus, justo ante finibus tortor, eget fermentum orci lacus nec dolor. Ut elementum risus a lobortis fringilla. Donec turpis tellus, lacinia at laoreet non, suscipit a mi. Nulla lacinia, ante et porta faucibus, massa risus cursus urna, quis convallis orci dui vel augue. Nam id velit accumsan, dignissim eros vitae, venenatis nunc. Cras ultrices odio non arcu viverra, et sollicitudin turpis rhoncus. Vestibulum eu tincidunt risus. Integer laoreet bibendum tellus eu dapibus. Praesent fermentum est at nisl viverra eleifend.  Sed tempor nisi leo, in condimentum erat ornare a. Phasellus aliquam ante nec risus porttitor cursus. Etiam quis magna suscipit justo elementum tristique. Curabitur congue fermentum risus, eu euismod ligula gravida vitae. Phasellus sit amet arcu ornare mauris faucibus vehicula. Sed sed odio nibh. Integer tristique sapien quis neque aliquet, eu bibendum lacus tristique. Praesent quis augue tellus. Phasellus tristique tortor in hendrerit tempus. Mauris vel felis lacinia, imperdiet diam et, suscipit mi. Duis et urna porta, molestie augue id, aliquet sa.";
  size_t src_size = 4000;
  size_t max_dst_size = ZSTD_compressBound(src_size);
  void *comp_data = malloc(sizeof(char) * src_size);
  printf("Max dst size: %zd\n", max_dst_size);

  // Compress it
  size_t comp_size = ZSTD_compress(comp_data, max_dst_size, data, src_size, 22);
  printf("Compression returned: %zd\n", comp_size);

  // Decompress it
  char *new_data = malloc(sizeof(char) * src_size);
  size_t new_src_size = ZSTD_decompress(new_data, src_size, comp_data, comp_size);
  printf("Decompression returned: %zd\n", new_src_size);
  printf("%s\n", new_data);

  // Test out the is error stuff.
  size_t err = -2;
  printf("Error is: %u\n", ZSTD_isError(err));
  if (ZSTD_isError(-2))
    printf("0 made out as true\n");
  return 0;
}
