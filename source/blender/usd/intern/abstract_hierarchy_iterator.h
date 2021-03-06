/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2019 Blender Foundation.
 * All rights reserved.
 */

/*
 * This file contains the AbstractHierarchyIterator. It is intended for exporters for file
 * formats that concern an entire hierarchy of objects (rather than, for example, an OBJ file that
 * contains only a single mesh). Examples are Universal Scene Description (USD) and Alembic.
 * AbstractHierarchyIterator is intended to be subclassed to support concrete file formats.
 *
 * The AbstractHierarchyIterator makes a distinction between the actual object hierarchy and the
 * export hierarchy. The former is the parent/child structure in Blender, which can have multiple
 * parent-like objects. For example, a duplicated object can have both a duplicator and a parent,
 * both determining the final transform. The export hierarchy is the hierarchy as written to the
 * file, and every object has only one export-parent.
 *
 * Currently the AbstractHierarchyIterator does not make any decisions about *what* to export.
 * Selections like "selected only" or "no hair systems" are left to concrete subclasses.
 */

#ifndef __USD__ABSTRACT_HIERARCHY_ITERATOR_H__
#define __USD__ABSTRACT_HIERARCHY_ITERATOR_H__

#include <map>
#include <string>
#include <set>

struct Base;
struct Depsgraph;
struct DupliObject;
struct ID;
struct Object;
struct ParticleSystem;
struct ViewLayer;

namespace USD {

class AbstractHierarchyWriter;

/* HierarchyContext structs are created by the AbstractHierarchyIterator. Each HierarchyContext
 * struct contains everything necessary to export a single object to a file. */
struct HierarchyContext {
  /*********** Determined during hierarchy iteration: ***************/
  Object *object;
  Object *export_parent;
  Object *duplicator;
  float matrix_world[4][4];
  std::string export_name;

  /* When weak_export=true, the object will be exported only as transform, and only if is an
   * ancestor of an object with weak_export=false.
   *
   * In other words: when weak_export=true but this object has no children, or all decendants also
   * have weak_export=true, this object (and by recursive reasoning all its decendants) will be
   * excluded from the export.
   *
   * The export hierarchy is kept as close to the the hierarchy in Blender as possible. As such, an
   * object that serves as a parent for another object, but which should NOT be exported itself, is
   * exported only as transform (i.e. as empty). This happens with objects that are part of a
   * holdout collection (which prevents them from being exported) but also parent of an exported
   * object. */
  bool weak_export;

  /* When true, this object should check its parents for animation data when determining whether
   * it's animated. This is necessary when a parent object in Blender is not part of the export. */
  bool animation_check_include_parent;

  /*********** Determined during writer creation: ***************/
  float parent_matrix_inv_world[4][4];  // Inverse of the parent's world matrix.
  std::string export_path;          // Hierarchical path, such as "/grandparent/parent/objectname".
  ParticleSystem *particle_system;  // Only set for particle/hair writers.

  /* Hierarchical path of the object this object is duplicating; only set when this object should
   * be stored as a reference to its original. It can happen that the original is not part of the
   * exported objects, in which case this string is empty even though 'duplicator' is set. */
  std::string original_export_path;

  bool operator<(const HierarchyContext &other) const;

  /* Return a HierarchyContext representing the root of the export hierarchy. */
  static const HierarchyContext *root();

  /* For handling instanced collections, instances created by particles, etc. */
  bool is_instance() const;
  void mark_as_instance_of(const std::string &reference_export_path);
  void mark_as_not_instanced();
};

/* Abstract writer for objects. Create concrete subclasses to write to USD, Alembic, etc.
 *
 * Instantiated by the AbstractHierarchyIterator on the first frame an object exists. Generally
 * that's the first frame to be exported, but can be later, for example when objects are
 * instantiated by particles. The AbstractHierarchyWriter::write() function is called on every
 * frame the object exists in the dependency graph and should be exported.
 */
class AbstractHierarchyWriter {
 public:
  virtual ~AbstractHierarchyWriter();
  virtual void write(HierarchyContext &context) = 0;
  // TODO(Sybren): add function like absent() that's called when a writer was previously created,
  // but wasn't used while exporting the current frame (for example, a particle-instanced mesh of
  // which the particle is no longer alive).
};

/* AbstractHierarchyIterator iterates over objects in a dependency graph, and constructs export
 * writers. These writers are then called to perform the actual writing to a USD or Alembic file.
 *
 * Dealing with file- and scene-level data (for example, creating a USD scene, setting the frame
 * rate, etc.) is not part of the AbstractHierarchyIterator class structure, and should be done
 * in separate code.
 */
class AbstractHierarchyIterator {
 public:
  /* Mapping from export path to writer. */
  typedef std::map<std::string, AbstractHierarchyWriter *> WriterMap;
  /* Pair of a duplicated object and its duplicator, typically a pair of HierarchyContext::object
   * and HierarchyContext::duplicator. */
  typedef std::pair<Object *, Object *> DupliAndDuplicator;
  /* All the children of some object, as per the export hierarchy. */
  typedef std::set<HierarchyContext *> ExportChildren;
  /* Mapping from an object and its duplicator to the object's export-children. */
  typedef std::map<DupliAndDuplicator, ExportChildren> ExportGraph;
  /* Mapping from (potential) duplicator ID to export path. */
  typedef std::map<ID *, std::string> ExportPathMap;

 protected:
  ExportGraph export_graph_;
  ExportPathMap duplisource_export_path_;
  Depsgraph *depsgraph_;
  WriterMap writers_;

 public:
  explicit AbstractHierarchyIterator(Depsgraph *depsgraph);
  virtual ~AbstractHierarchyIterator();

  /* Iterate over the depsgraph, create writers, and tell the writers to write.
   * Main entry point for the AbstractHierarchyIterator, must be called for every to-be-exported
   * frame. */
  void iterate_and_write();

  /* Release all writers. Call after all frames have been exported. */
  void release_writers();

  /* Convert the given name to something that is valid for the exported file format.
   * This base implementation is a no-op; override in a concrete subclass. */
  virtual std::string make_valid_name(const std::string &name) const;

  /* Return the name of this ID datablock that is valid for the exported file format. Overriding is
   * only necessary if make_valid_name(id->name+2) is not suitable for the exported file format.
   * NULL-safe: when `id == nullptr` this returns an empty string. */
  virtual std::string get_id_name(const ID *id) const;

  /* Given a HierarchyContext of some Object *, return an export path that is valid for its
   * object->data. Overriding is necessary when the exported format does NOT expect the object's
   * data to be a child of the object. */
  virtual std::string get_object_data_path(const HierarchyContext *context) const;

 private:
  void debug_print_export_graph() const;

  void export_graph_construct();
  void export_graph_prune();
  void export_graph_clear();

  void visit_object(Object *object, Object *export_parent, bool weak_export);
  void visit_dupli_object(DupliObject *dupli_object,
                          Object *duplicator,
                          const std::set<Object *> &dupli_set);

  ExportChildren &graph_children(const HierarchyContext *parent_context);

  void determine_export_paths(const HierarchyContext *parent_context);
  void determine_duplication_references(const HierarchyContext *parent_context,
                                        std::string indent);

  void make_writers(const HierarchyContext *parent_context);
  void make_writer_object_data(const HierarchyContext *context);
  void make_writers_particle_systems(const HierarchyContext *context);

  /* Convenience wrappers around get_id_name(). */
  std::string get_object_name(const Object *object) const;
  std::string get_object_data_name(const Object *object) const;

  AbstractHierarchyWriter *get_writer(const std::string &export_path);

  typedef AbstractHierarchyWriter *(AbstractHierarchyIterator::*create_writer_func)(
      const HierarchyContext *);
  /* Ensure that a writer exists; if it doesn't, call create_func(context). The create_func
   * function should be one of the create_XXXX_writer(context) functions declared below. */
  AbstractHierarchyWriter *ensure_writer(HierarchyContext *context,
                                         create_writer_func create_func);

 protected:
  /* Construct a valid path for the export file format. This class concatenates by using '/' as a
   * path separator, which is valid for both Alembic and USD. */
  virtual std::string path_concatenate(const std::string &parent_path,
                                       const std::string &child_path) const;

  /* Return whether this object should be marked as 'weak export' or not.
   *
   * When this returns false, writers for the transform and data are created,
   * and dupli-objects dupli-object generated from this object will be passed to
   * should_visit_dupli_object().
   *
   * When this returns true, only a transform writer is created and marked as
   * 'weak export'. In this case, the transform writer will be removed before
   * exporting starts, unless a decendant of this object is to be exported.
   * Dupli-object generated from this object will also be skipped.
   *
   * See HierarchyContext::weak_export.
   */
  virtual bool mark_as_weak_export(const Object *object) const;

  virtual bool should_visit_dupli_object(const DupliObject *dupli_object) const;

  /* These functions should create an AbstractHierarchyWriter subclass instance, or return
   * nullptr if the object or its data should not be exported. Returning a nullptr for
   * data/hair/particle will NOT prevent the transform to be written.
   *
   * The returned writer is owned by the AbstractHierarchyWriter, and should be freed in
   * delete_object_writer(). */
  virtual AbstractHierarchyWriter *create_transform_writer(const HierarchyContext *context) = 0;
  virtual AbstractHierarchyWriter *create_data_writer(const HierarchyContext *context) = 0;
  virtual AbstractHierarchyWriter *create_hair_writer(const HierarchyContext *context) = 0;
  virtual AbstractHierarchyWriter *create_particle_writer(const HierarchyContext *context) = 0;

  /* Called by release_writers() to free what the create_XXX_writer() functions allocated. */
  virtual void delete_object_writer(AbstractHierarchyWriter *writer) = 0;
};

}  // namespace USD

#endif /* __USD__ABSTRACT_HIERARCHY_ITERATOR_H__ */
