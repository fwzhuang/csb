#include "CSBmesh.h"
#include "gtx/fast_square_root.hpp"
#include "gtx/norm.hpp"

std::unordered_set<CSBmesh::EdgePair> CSBmesh::getEdges()
{
  std::unordered_set<EdgePair> edgeSet;
  auto numEdges = m_vertices.size() + (m_indices.size() / 3) - 2;
  edgeSet.reserve(numEdges);

  const auto last = m_indices.size() - 2;
  for (size_t i = 0; i < last; i+=3)
  {
    const auto p1 = m_indices[i];
    const auto p2 = m_indices[i + 1];
    const auto p3 = m_indices[i + 2];
    edgeSet.insert({p1, p2});
    edgeSet.insert({p2, p3});
    edgeSet.insert({p3, p1});
  }
  return edgeSet;
}


std::vector<GLushort> CSBmesh::getConnectedVertices(const GLushort _vert)
{
  return m_adjacency[_vert];
}

glm::ivec3 CSBmesh::calcCell(const glm::vec3& _coord) const
{
  // cellsize is equal to the average edge length for max performance
  return glm::ivec3(
        static_cast<int>(glm::floor(_coord.x / m_avgEdgeLength)),
        static_cast<int>(glm::floor(_coord.y / m_avgEdgeLength)),
        static_cast<int>(glm::floor(_coord.z / m_avgEdgeLength))
        );
}

size_t CSBmesh::hashCell (const glm::ivec3& _cell) const
{
  static constexpr auto posMod = [](const auto _x, const auto _m)
  {
    return ((static_cast<size_t>(_x) % _m) + _m) % _m;
  };

  static constexpr int primes[] = {73856093, 19349663, 83492791};
  return posMod((_cell.x * primes[0]) ^ (_cell.y * primes[1]) ^ (_cell.z * primes[2]), m_hashTable.size());
}

size_t CSBmesh::hashPoint (const glm::vec3& _coord) const
{
  return hashCell(calcCell(_coord));
}

void CSBmesh::hashVerts()
{
  for (auto& cell : m_hashTable) cell.clear();
  for (GLushort i = 0; i < m_points.size(); ++i)
  {
    m_hashTable[hashPoint(m_points[i].m_pos)].push_back(i);
  }
}

void CSBmesh::hashTris()
{
  for (auto& hash : m_triangleVertHash) hash.clear();
  const auto size = m_triangleVertHash.size();
  for (size_t i = 0; i < size; ++i)
  {
    const size_t index = i * 3;
    const auto& p1 = m_points[m_indices[index]];
    const auto& p2 = m_points[m_indices[index + 1]];
    const auto& p3 = m_points[m_indices[index + 2]];

    const auto min = calcCell(glm::min(glm::min(p1.m_pos, p2.m_pos), p3.m_pos));
    const auto max = calcCell(glm::max(glm::max(p1.m_pos, p2.m_pos), p3.m_pos));

    // hash all cells within the bounding box of this triangle
    for (int x = min.x; x <= max.x; ++x)
      for (int y = min.y; y <= max.y; ++y)
        for (int z = min.z; z <= max.z; ++z)
        {
          m_triangleVertHash[i].push_back(hashCell({x,y,z}));
        }
  }
}

void CSBmesh::resolveSelfCollision_spheres()
{
  const auto size = m_points.size();
  for (GLushort i = 0; i < size; ++i)
  {
    auto& P = m_points[i];


    auto ignored = getConnectedVertices(i);
    ignored.push_back(i);
    std::sort(ignored.begin(), ignored.end());

    auto considered = m_hashTable[hashPoint(P.m_pos)];
    std::sort(considered.begin(), considered.end());

    // Scope the using declaration
    {
      // I think this is more readable
      using namespace std;
      considered.erase(
            remove_if(begin(considered), end(considered),
                      [&ignored](const auto x) { return binary_search(begin(ignored), end(ignored),x); }),
            end(considered)
            );
    }


    glm::vec3 offset(0.f);
    int count = 0;
    for (const auto& pid : considered)
    {
      const auto& Q = m_points[pid];
      const auto disp = P.m_pos - Q.m_pos;
      const auto dist = glm::length2(disp);

      auto double_radius_sqr = (m_avgEdgeLength * 1.2f);
      double_radius_sqr *= double_radius_sqr;
      if (dist < double_radius_sqr)
      {
        const auto move = (glm::fastSqrt(double_radius_sqr) - glm::fastSqrt(dist)) * 0.5f;
        offset += (glm::fastNormalize(disp) * move);
        ++count;
      }
    }

    if (count)
    {
      P.m_pos += offset/static_cast<float>(count);
      // zero the velocity
      P.m_prevPos = P.m_pos;
    }
  }
}

void CSBmesh::resolveSelfCollision_rays()
{
  const auto size = m_triangleVertHash.size();
  //     Loop over all faces
  for (size_t i = 0; i < size; ++i)
  {
    const size_t index = i * 3;
    const auto& T0 = m_points[m_indices[index]].m_pos;
    const auto& T1 = m_points[m_indices[index + 1]].m_pos;
    const auto& T2 = m_points[m_indices[index + 2]].m_pos;
    const auto edge1 = T1 - T0;
    const auto edge2 = T2 - T0;
    const auto edge3 = T2 - T1;
    const auto norm = glm::fastNormalize(glm::cross(edge1, edge2));

    // Loop over all hashed cells for this face
    for (const auto& hash : m_triangleVertHash[i])
    {
      // Loop over all points in the cell
      for (const auto& pid : m_hashTable[hash])
      {
        // skip the points in this face
        if ((pid == m_indices[index]) || (pid == m_indices[index + 1]) || (pid == m_indices[index + 2]))
          continue;
        const auto& point = m_points[pid];
        const auto& L0 = point.m_prevPos;
        const auto& L1 = point.m_pos;

        const auto DistStart = glm::dot(L0 - T0, norm);
        const auto DistEnd = glm::dot(L1 - T0, norm);

        const auto intersection = L0 + (L1 - L0) * (-DistStart / (DistEnd - DistStart));

        const auto X1 = glm::dot(glm::cross(norm, edge1), intersection - T0);
        const auto X2 = glm::dot(glm::cross(norm, edge3), intersection - T1);
        const auto X3 = glm::dot(glm::cross(norm, -edge2), intersection - T0);

        bool insideTri = (X1 >= 0.0f) && (X2 >= 0.0f) && (X3 >= 0.0f);

        // Check not same side of triangle
        if ((DistStart * DistEnd < 0.0f) && insideTri)
        {
          // Add constraint here
          std::swap(m_points[m_indices[index]].m_pos, m_points[m_indices[index]].m_prevPos);
          std::swap(m_points[m_indices[index + 1]].m_pos, m_points[m_indices[index + 1]].m_prevPos);
          std::swap(m_points[m_indices[index + 2]].m_pos, m_points[m_indices[index + 2]].m_prevPos);
          std::swap(m_points[pid].m_pos, m_points[pid].m_prevPos);
          //          m_points[pid].m_pos = intersection + glm::normalize(intersection - L1) * 0.02f;
        }
      }
    }
  }
}

void CSBmesh::init()
{
  m_triangleVertHash.resize(m_indices.size() / 3);

  // Calculate optimal hash table size
  const auto numVerts = m_vertices.size();
  const size_t multiple = static_cast<size_t>(pow10(floor(log10(numVerts))));
  const auto hashTableSize = ((numVerts + multiple - 1) / multiple) * multiple - 1;
  m_hashTable.resize(hashTableSize);
  m_points.reserve(m_vertices.size());

  for (auto& vert : m_vertices)
    m_points.emplace_back(vert, 1.f);

  m_points[0].m_invMass = 0.f;
//    m_points[90].m_invMass = 0.f;
  //  m_points[24].m_invMass = 0.f;
  m_points[m_points.size() - 1].m_invMass = 0.f;

  auto edgeSet = getEdges();
  float totalEdgeDist = 0.0f;
  for (const auto & edge : edgeSet)
  {
    const auto p1 = edge.p.first;
    const auto p2 = edge.p.second;
    const auto distance = glm::fastDistance(m_vertices[p1], m_vertices[p2]);
    totalEdgeDist += distance;
    m_constraints.emplace_back(new DistanceConstraint(p1, p2, distance));
  }

  m_avgEdgeLength = totalEdgeDist / edgeSet.size();

  const auto size = m_vertices.size();
  std::unordered_set<EdgePair> connections;
  for (GLushort v = 0; v < size; ++v)
  {
    auto neighbours = getConnectedVertices(v);
    for (const auto vi : neighbours)
    {
      float bestCosTheta = 0.0f;
      auto bestV = vi;
      for (const auto vj : neighbours)
      {
        if (vj == vi) continue;
        auto a = m_vertices[vi] - m_vertices[v];
        auto b = m_vertices[vj] - m_vertices[v];
        auto cosTheta = glm::dot(a, b) / (glm::fastLength(a) * glm::fastLength(b));
        if (cosTheta < bestCosTheta)
        {
          bestCosTheta = cosTheta;
          bestV = vj;
        }
      }
      EdgePair connection {bestV, vi};
      if (!connections.count(connection))
      {
        connections.insert(connection);
        static constexpr float third = 1.0f / 3.0f;
        auto centre = third * (m_vertices[vi] + m_vertices[bestV] + m_vertices[v]);
        auto rest = glm::fastDistance(m_vertices[v], centre);
        m_constraints.emplace_back(new BendingConstraint(vi, bestV, v, rest, m_points));
      }
    }
  }
}

void CSBmesh::update(const float _time)
{
  for (int i = 0; i < 10; ++i)
    for (auto& constraint : m_constraints)
    {
      constraint->project(m_points);
    }

  const auto gravity = glm::vec3(0.f,-1.f,0.f);
  const auto size = m_points.size();


  for (size_t i = 0; i < size; ++i)
  {
    auto& point = m_points[i];
    glm::vec3 newPos = point.m_pos * 2.0f - point.m_prevPos + (point.m_invMass * gravity * _time * _time);
    point.m_prevPos = point.m_pos;
    point.m_pos = newPos;
  }


  hashVerts();
  hashTris();

  resolveSelfCollision_rays();

  //  hashVerts();
  //  hashTris();

  resolveSelfCollision_spheres();

}
