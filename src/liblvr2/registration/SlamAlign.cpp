/**
 * Copyright (c) 2018, University Osnabrück
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the University Osnabrück nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL University Osnabrück BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * SlamAlign.cpp
 *
 *  @date May 6, 2019
 *  @author Malte Hillmann
 */
#include <lvr2/registration/SlamAlign.hpp>

#include <iomanip>

using namespace std;

namespace lvr2
{

SlamAlign::SlamAlign(const SlamOptions& options, int count)
    : m_options(options)
{
    if (count > 0)
    {
        m_scans.resize(count);
    }
}

void SlamAlign::addScan(const ScanPtr& scan)
{
    reduceScan(scan);
    m_scans.push_back(scan);
}
void SlamAlign::setScan(int index, const ScanPtr& scan)
{
    reduceScan(scan);
    m_scans[index] = scan;
}

void SlamAlign::reduceScan(const ScanPtr& scan)
{
    size_t prev = scan->count();
    if (m_options.reduction >= 0)
    {
        scan->reduce(m_options.reduction);
    }
    if (m_options.minDistance >= 0)
    {
        scan->setMinDistance(m_options.minDistance);
    }
    if (m_options.maxDistance >= 0)
    {
        scan->setMaxDistance(m_options.maxDistance);
    }

    if (scan->count() < prev && m_options.verbose)
    {
        cout << "Removed " << (prev - scan->count()) << " / " << prev << " Points -> " << scan->count() << " left" << endl;
    }
}

void SlamAlign::match()
{
    if (m_options.metascan)
    {
        m_metascan = ScanPtr(new Scan(*m_scans[0]));
    }

    string scan_number_string = to_string(m_scans.size() - 1);
    for (int i = 1; i < m_scans.size(); i++)
    {
        if (m_options.verbose)
        {
            cout << "Iteration " << setw(scan_number_string.length()) << i << "/" << scan_number_string << ": " << endl;
        }
        else
        {
            cout << setw(scan_number_string.length()) << i << "/" << scan_number_string << ": " << flush;
        }

        ScanPtr prev = m_options.metascan ? m_metascan : m_scans[i - 1];
        const ScanPtr& cur = m_scans[i];

        if (!m_options.trustPose && i != 1) // no deltaPose on first run
        {
            applyTransform(cur, prev->getDeltaPose());
        }
        else
        {
            addFrame(cur);
        }

        ICPPointAlign icp(prev, cur);
        icp.setMaxMatchDistance(m_options.icpMaxDistance);
        icp.setMaxIterations(m_options.icpIterations);
        icp.setEpsilon(m_options.epsilon);
        icp.setVerbose(m_options.verbose);

        icp.match();

        addFrame(cur);

        if (m_options.metascan && i + 1 < m_scans.size())
        {
            m_metascan->addScanToMeta(cur);
        }

        checkLoopClose(i);
    }
}

void SlamAlign::applyTransform(ScanPtr scan, const Matrix4f& transform)
{
    scan->transform(transform);

    bool found = false;
    for (const ScanPtr& s : m_scans)
    {
        if (s != scan)
        {
            s->addFrame(found ? ScanUse::INVALID : ScanUse::UNUSED);
        }
        else
        {
            found = true;
        }
    }
}

void SlamAlign::addFrame(ScanPtr current)
{
    bool found = false;
    for (const ScanPtr& s : m_scans)
    {
        if (s != current)
        {
            s->addFrame(found ? ScanUse::INVALID : ScanUse::UNUSED);
        }
        else
        {
            s->addFrame(ScanUse::UPDATED);
            found = true;
        }
    }
}

void SlamAlign::checkLoopClose(int last)
{
    if (!m_options.doLoopClosing && !m_options.doGraphSlam)
    {
        return;
    }

    vector<int> others;
    findCloseScans(last, others);

    if (others.empty())
    {
        m_foundLoop = 0;
        return;
    }

    for (int i : others)
    {
        m_graph.push_back(make_pair(i, last));
    }

    auto iter = find_if(others.begin(), others.end(), [&](int i)
    {
        return last - i > m_options.loopSize;
    });

    if (iter == others.end())
    {
        m_foundLoop = 0;
        return;
    }

    m_foundLoop++;

    if (m_foundLoop < 2)
    {
        return;
    }

    int first = *iter;

    if (m_options.doLoopClosing)
    {
        loopClose(first, last);
    }
    if (m_options.doGraphSlam)
    {
        graphSlam(last);
    }
}

Matrix6f SlamAlign::eulerCovariance(int a, int b) const
{
    const ScanPtr& scanA = m_scans[a];
    const ScanPtr& scanB = m_scans[b];

    size_t n = scanB->count();

    auto tree = KDTree::create(scanA->points(), scanA->count());
    Vector3f* points = scanB->points();
    Vector3f** results = new Vector3f*[n];
    getNearestNeighbors(tree, points, results, n, m_options.slamMaxDistance);

    Vector6f mz = Vector6f::Zero();
    Vector3f sum, sumProducts, sumSquares;
    sum = sumProducts = sumSquares = Vector3f::Zero();

    for (size_t i = 0; i < n; i++)
    {
        if (results[i] == nullptr)
        {
            continue;
        }
        Vector3f mid = (points[i] + *results[i]) / 2.0f;
        Vector3f delta = *results[i] - points[i];

        sum += mid;

        sumProducts.x() += mid.x() * mid.y();
        sumProducts.y() += mid.y() * mid.z();
        sumProducts.z() += mid.z() * mid.x();

        Vector3f squared = mid.cwiseProduct(mid);

        sumSquares.x() += squared.x() +  squared.y();
        sumSquares.y() += squared.y() +  squared.z();
        sumSquares.z() += squared.z() +  squared.x();

        mz.block<3, 1>(0, 0) += delta;

        Vector3f cross = mid.cross(delta);
        mz(3) += cross.x();
        mz(4) += cross.z();
        mz(5) += cross.y();
    }

    Matrix6f mm = Matrix6f::Zero();
    mm(0, 0) = mm(1, 1) = mm(2, 2) = n;
    mm(3, 3) = sumSquares.y();
    mm(4, 4) = sumSquares.x();
    mm(5, 5) = sumSquares.z();

    mm(0, 4) = mm(4, 0) = -sum.y();
    mm(0, 5) = mm(5, 0) = sum.z();
    mm(1, 3) = mm(3, 1) = -sum.z();
    mm(1, 4) = mm(4, 1) = sum.x();
    mm(2, 3) = mm(3, 2) = sum.y();
    mm(2, 5) = mm(5, 2) = -sum.x();

    mm(3, 4) = mm(4, 3) = -sumProducts.z();
    mm(3, 5) = mm(5, 3) = -sumProducts.y();
    mm(4, 5) = mm(5, 4) = -sumProducts.x();

    Vector6f d = mm.inverse() * mz;

    float ss = 0.0f;

    for (size_t i = 0; i < n; i++)
    {
        if (results[i] == nullptr)
        {
            continue;
        }

        Vector3f mid = (points[i] + *results[i]) / 2.0f;

        Vector3f s(d(0) - mid.y() * d(4) + mid.z() * d(5),
                   d(1) - mid.z() * d(3) + mid.x() * d(4),
                   d(2) - mid.x() * d(5) + mid.y() * d(3));

        s = points[i] - *results[i] - s;

        ss = s.squaredNorm();
    }

    delete results;

    ss = ss / ( 2.0f * n - 3.0f);

    ss = 1.0f / ss;

    return mm * ss;
}

void SlamAlign::loopClose(int first, int last)
{
    cout << "LOOPCLOSE!!!!" << first << " -> " << last << endl;

    ScanPtr metaFirst = make_shared<Scan>(*m_scans[first]);
    metaFirst->addScanToMeta(m_scans[first + 1]);
    metaFirst->addScanToMeta(m_scans[first + 2]);

    ScanPtr metaLast = make_shared<Scan>(*m_scans[last]);
    metaLast->addScanToMeta(m_scans[last - 1]);
    metaLast->addScanToMeta(m_scans[last - 2]);

    ICPPointAlign icp(metaFirst, metaLast);
    icp.setMaxMatchDistance(m_options.slamMaxDistance);
    icp.setMaxIterations(m_options.slamIterations);
    icp.setEpsilon(m_options.epsilon);
    icp.setVerbose(m_options.verbose);

    Matrix4f transform = icp.match();

    cout << "Loopclose delta: " << endl << transform << endl << endl;

    // TODO: Calculate Covariance
    // TODO: Emulate graph_balancer
    // TODO: Calculate weights
    // TODO: Transform based on weights

    for (int i = first; i <= last; i++)
    {
        float factor = (i - first) / (float)(last - first);

        Matrix4f delta = (transform - Matrix4f::Identity()) * factor + Matrix4f::Identity();

        m_scans[i]->transform(delta, true, ScanUse::LOOPCLOSE);
    }

    // Add frame to unaffected scans
    for (int i = 0; i < first; i++)
    {
        m_scans[i]->addFrame();
    }
    for (int i = last + 1; i < m_scans.size(); i++)
    {
        m_scans[i]->addFrame(ScanUse::INVALID);
    }
}

void SlamAlign::graphSlam(int last)
{
    // TODO: implement
}

void SlamAlign::findCloseScans(int scan, vector<int>& output)
{
    ScanPtr& cur = m_scans[scan];

    // closeLoopPairs not specified => use closeLoopDistance
    if (m_options.closeLoopPairs < 0)
    {
        float maxDist = std::pow(m_options.closeLoopDistance, 2);
        Vector3f pos = cur->getPosition();
        for (int other = 0; other < scan; other++)
        {
            float dist = (m_scans[other]->getPosition() - pos).squaredNorm();
            if (dist < maxDist)
            {
                output.push_back(other);
            }
        }
    }
    else
    {
        // convert current Scan to KDTree for Pair search
        size_t n = cur->count();
        auto tree = KDTree::create(cur->points(), cur->count());

        for (int other = 0; other < scan; other++)
        {
            const ScanPtr& scan = m_scans[other];
            int count = 0;

            #pragma omp parallel for reduction(+:count)
            for (size_t i = 0; i < scan->count(); i++)
            {
                const Vector3f& point = scan->getPoint(i);
                Vector3f* neighbor = nullptr;
                float dist;
                if (tree->nearestNeighbor(point, neighbor, dist, m_options.slamMaxDistance))
                {
                    count++;
                }
            }

            if (count >= m_options.closeLoopPairs)
            {
                output.push_back(other);
            }
        }
    }
}

} /* namespace lvr2 */
