#ifndef MESHBLOCKTREE_HPP
#define MESHBLOCKTREE_HPP
//======================================================================================
// Athena++ astrophysical MHD code
// Copyright (C) 2014 James M. Stone  <jmstone@princeton.edu>
// See LICENSE file for full public license information.
//======================================================================================
//! \file meshblocktree.hpp
//  \brief defines the LogicalLocation structure and MeshBlockTree class
//======================================================================================

// Athena++ classes headers
#include "athena.hpp"
#include "athena_arrays.hpp"
#include "defs.hpp"
#include "bvals/bvals.hpp"

//! \class MeshBlockTree
//  \brief Construct AMR Block tree structure
class MeshBlockTree
{
private:
  bool flag; // false: vitrual, has leaves, true: real, is a leaf
  MeshBlockTree* pparent;
  MeshBlockTree* pleaf[2][2][2];
  LogicalLocation loc;
  int gid;
  friend class MeshBlock;
public:
  MeshBlockTree();
  MeshBlockTree(MeshBlockTree *parent, int ox, int oy, int oz);
  ~MeshBlockTree();
  void CreateRootGrid(long int nx, long int ny, long int nz, int nl);
  void AddMeshBlock(MeshBlockTree& root, LogicalLocation rloc, int dim,
    enum BoundaryFlag* mesh_bcs, long int rbx, long int rby, long int rbz, int rl);
  void AddMeshBlockWithoutRefine(MeshBlockTree& root, LogicalLocation rloc, int dim,
    enum BoundaryFlag* mesh_bcs, long int rbx, long int rby, long int rbz, int rl);
  void Refine(MeshBlockTree& root, int dim, enum BoundaryFlag* mesh_bcs,
              long int rbx, long int rby, long int rbz, int rl);
  void Derefine(void);
  void AssignGID(int& id);
  void GetLocationList(LogicalLocation *list, int& count);
  MeshBlockTree* FindNeighbor(LogicalLocation myloc, int ox1, int ox2, int ox3, 
    enum BoundaryFlag* bcs, long int rbx, long int rby, long int rbz, int rl);
  MeshBlockTree* GetLeaf(int ox, int oy, int oz);
};

#endif

