#ifndef RXPUTIL_H
#define RXPUTIL_H

#define Vector(type, name) \
    int name##_count, name##_alloc; \
    type *name

#define VectorInit(v) \
    (v##_count = v##_alloc = 0, v = 0)

#define VectorPush(v, value) \
    ((v##_count < v##_alloc || \
      (v = VectorExtend(v))) ? \
     (v[v##_count++] = (value), 1) : \
     0)

#define VectorExtend(v) \
    (v##_alloc = (v##_alloc == 0 ? 8 : v##_alloc * 2), \
     Realloc(v, v##_alloc * sizeof(v)))

#define VectorCount(v) v##_count

#endif /* RXPUTIL_H */
