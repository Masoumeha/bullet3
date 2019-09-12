/*
 Written by Xuchen Han <xuchenhan2015@u.northwestern.edu>
 
 Bullet Continuous Collision Detection and Physics Library
 Copyright (c) 2019 Google Inc. http://bulletphysics.org
 This software is provided 'as-is', without any express or implied warranty.
 In no event will the authors be held liable for any damages arising from the use of this software.
 Permission is granted to anyone to use this software for any purpose,
 including commercial applications, and to alter it and redistribute it freely,
 subject to the following restrictions:
 1. The origin of this software must not be misrepresented; you must not claim that you wrote the original software. If you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.
 2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.
 3. This notice may not be removed or altered from any source distribution.
 */

#include "btDeformableContactConstraint.h"

/* ================   Deformable vs. Rigid   =================== */
btDeformableRigidContactConstraint::btDeformableRigidContactConstraint(const btSoftBody::DeformableRigidContact& c)
: m_contact(&c)
, btDeformableContactConstraint(c.m_cti.m_normal)
{
    m_total_normal_dv.setZero();
    m_total_tangent_dv.setZero();
}

btDeformableRigidContactConstraint::btDeformableRigidContactConstraint(const btDeformableRigidContactConstraint& other)
: m_contact(other.m_contact)
, btDeformableContactConstraint(other)
{
    m_total_normal_dv = other.m_total_normal_dv;
    m_total_tangent_dv = other.m_total_tangent_dv;
}


btVector3 btDeformableRigidContactConstraint::getVa() const
{
    const btSoftBody::sCti& cti = m_contact->m_cti;
    btVector3 va(0, 0, 0);
    if (cti.m_colObj->hasContactResponse())
    {
        btRigidBody* rigidCol = 0;
        btMultiBodyLinkCollider* multibodyLinkCol = 0;
        
        // grab the velocity of the rigid body
        if (cti.m_colObj->getInternalType() == btCollisionObject::CO_RIGID_BODY)
        {
            rigidCol = (btRigidBody*)btRigidBody::upcast(cti.m_colObj);
            va = rigidCol ? (rigidCol->getVelocityInLocalPoint(m_contact->m_c1)) : btVector3(0, 0, 0);
        }
        else if (cti.m_colObj->getInternalType() == btCollisionObject::CO_FEATHERSTONE_LINK)
        {
            multibodyLinkCol = (btMultiBodyLinkCollider*)btMultiBodyLinkCollider::upcast(cti.m_colObj);
            if (multibodyLinkCol)
            {
                const int ndof = multibodyLinkCol->m_multiBody->getNumDofs() + 6;
                const btScalar* J_n = &m_contact->jacobianData_normal.m_jacobians[0];
                const btScalar* J_t1 = &m_contact->jacobianData_t1.m_jacobians[0];
                const btScalar* J_t2 = &m_contact->jacobianData_t2.m_jacobians[0];
                const btScalar* local_v = multibodyLinkCol->m_multiBody->getVelocityVector();
                const btScalar* local_dv = multibodyLinkCol->m_multiBody->getDeltaVelocityVector();
                // add in the normal component of the va
                btScalar vel = 0.0;
                for (int k = 0; k < ndof; ++k)
                {
                    vel += (local_v[k]+local_dv[k]) * J_n[k];
                }
                va = cti.m_normal * vel;
                // add in the tangential components of the va
                vel = 0.0;
                for (int k = 0; k < ndof; ++k)
                {
                    vel += (local_v[k]+local_dv[k]) * J_t1[k];
                }
                va += m_contact->t1 * vel;
                vel = 0.0;
                for (int k = 0; k < ndof; ++k)
                {
                    vel += (local_v[k]+local_dv[k]) * J_t2[k];
                }
                va += m_contact->t2 * vel;
            }
        }
    }
    return va;
}

btScalar btDeformableRigidContactConstraint::solveConstraint()
{
    const btSoftBody::sCti& cti = m_contact->m_cti;
    btVector3 va = getVa();
    btVector3 vb = getVb();
    btVector3 vr = vb - va;
    const btScalar dn = btDot(vr, cti.m_normal);
//    if (dn > 0)
//        return 0;
    // dn is the normal component of velocity diffrerence. Approximates the residual. // todo xuchenhan@: this prob needs to be scaled by dt
    btScalar residualSquare = dn*dn;
    btVector3 impulse = m_contact->m_c0 * vr;
    const btVector3 impulse_normal = m_contact->m_c0 * (cti.m_normal * dn);
    btVector3 impulse_tangent = impulse - impulse_normal;
    
    btVector3 old_total_tangent_dv = m_total_tangent_dv;
    // m_c2 is the inverse mass of the deformable node/face
    m_total_normal_dv -= impulse_normal * m_contact->m_c2;
    m_total_tangent_dv -= impulse_tangent * m_contact->m_c2;
    
    if (m_total_normal_dv.dot(cti.m_normal) < 0)
    {
        // separating in the normal direction
        m_static = false;
        m_total_tangent_dv = btVector3(0,0,0);
        impulse_tangent.setZero();
    }
    else
    {
        if (m_total_normal_dv.norm() * m_contact->m_c3 < m_total_tangent_dv.norm())
        {
            // dynamic friction
            // with dynamic friction, the impulse are still applied to the two objects colliding, however, it does not pose a constraint in the cg solve, hence the change to dv merely serves to update velocity in the contact iterations.
            m_static = false;
            if (m_total_tangent_dv.norm() < SIMD_EPSILON)
            {
                m_total_tangent_dv = btVector3(0,0,0);
            }
            else
            {
                m_total_tangent_dv = m_total_tangent_dv.normalized() * m_total_normal_dv.norm() * m_contact->m_c3;
            }
            impulse_tangent = -btScalar(1)/m_contact->m_c2 * (m_total_tangent_dv - old_total_tangent_dv);
        }
        else
        {
            // static friction
            m_static = true;
        }
    }
    impulse = impulse_normal + impulse_tangent;
    applyImpulse(impulse);
    
    if (cti.m_colObj->getInternalType() == btCollisionObject::CO_RIGID_BODY)
    {
        btRigidBody* rigidCol = 0;
        rigidCol = (btRigidBody*)btRigidBody::upcast(cti.m_colObj);
        if (rigidCol)
        {
            rigidCol->applyImpulse(impulse, m_contact->m_c1);
        }
    }
    else if (cti.m_colObj->getInternalType() == btCollisionObject::CO_FEATHERSTONE_LINK)
    {
        btMultiBodyLinkCollider* multibodyLinkCol = 0;
        multibodyLinkCol = (btMultiBodyLinkCollider*)btMultiBodyLinkCollider::upcast(cti.m_colObj);
        if (multibodyLinkCol)
        {
            const btScalar* deltaV_normal = &m_contact->jacobianData_normal.m_deltaVelocitiesUnitImpulse[0];
            // apply normal component of the impulse
            multibodyLinkCol->m_multiBody->applyDeltaVeeMultiDof2(deltaV_normal, impulse.dot(cti.m_normal));
            if (impulse_tangent.norm() > SIMD_EPSILON)
            {
                // apply tangential component of the impulse
                const btScalar* deltaV_t1 = &m_contact->jacobianData_t1.m_deltaVelocitiesUnitImpulse[0];
                multibodyLinkCol->m_multiBody->applyDeltaVeeMultiDof2(deltaV_t1, impulse.dot(m_contact->t1));
                const btScalar* deltaV_t2 = &m_contact->jacobianData_t2.m_deltaVelocitiesUnitImpulse[0];
                multibodyLinkCol->m_multiBody->applyDeltaVeeMultiDof2(deltaV_t2, impulse.dot(m_contact->t2));
            }
        }
    }
    return residualSquare;
}

/* ================   Node vs. Rigid   =================== */
btDeformableNodeRigidContactConstraint::btDeformableNodeRigidContactConstraint(const btSoftBody::DeformableNodeRigidContact& contact)
    : m_node(contact.m_node)
    , btDeformableRigidContactConstraint(contact)
    {
    }

btDeformableNodeRigidContactConstraint::btDeformableNodeRigidContactConstraint(const btDeformableNodeRigidContactConstraint& other)
: m_node(other.m_node)
, btDeformableRigidContactConstraint(other)
{
}

btVector3 btDeformableNodeRigidContactConstraint::getVb() const
{
    return m_node->m_v;
}


btVector3 btDeformableNodeRigidContactConstraint::getDv(const btSoftBody::Node* node) const
{
    return m_total_normal_dv + m_total_tangent_dv;
}

/* ================   Face vs. Rigid   =================== */
btDeformableFaceRigidContactConstraint::btDeformableFaceRigidContactConstraint(const btSoftBody::DeformableFaceRigidContact& contact)
: m_face(contact.m_face)
, m_solved(false)
, btDeformableRigidContactConstraint(contact)
{
}

btDeformableFaceRigidContactConstraint::btDeformableFaceRigidContactConstraint(const btDeformableFaceRigidContactConstraint& other)
: m_face(other.m_face)
, m_solved(false)
, btDeformableRigidContactConstraint(other)
{
}

btVector3 btDeformableFaceRigidContactConstraint::getVb() const
{
    const btSoftBody::DeformableFaceRigidContact* contact = getContact();
    btVector3 vb = m_face->m_n[0]->m_v * contact->m_bary[0] + m_face->m_n[1]->m_v * contact->m_bary[1] + m_face->m_n[2]->m_v * contact->m_bary[2];
    return vb;
}


btVector3 btDeformableFaceRigidContactConstraint::getDv(const btSoftBody::Node* node) const
{
    btVector3 face_dv = m_total_normal_dv + m_total_tangent_dv;
    const btSoftBody::DeformableFaceRigidContact* contact = getContact();
    if (m_face->m_n[0] == node)
    {
        return face_dv * contact->m_weights[0];
    }
    if (m_face->m_n[1] == node)
    {
        return face_dv * contact->m_weights[1];
    }
    btAssert(node == m_face->m_n[2]);
    return face_dv * contact->m_weights[2];
}
