// SPDX-License-Identifier: GPL-2.0-or-later

#include "detector.hpp"

#include "../advanced/algorithm/gaussian_fitting.hpp"
#include "../interface.hpp"
#include "../neutral.hpp"
#include "algorithms.hpp"
#include "cluster.hpp"

#include <common/types.hpp>
#include <container/ops.hpp>
#include <math/mat2.hpp>
#include <math/vec2.hpp>

#include <algorithm>
#include <cmath>
#include <gsl/gsl>
#include <stdexcept>
#include <vector>

namespace iptsd::contacts::basic {

const std::vector<Blob> &BlobDetector::search()
{
	this->maximas.clear();
	this->clusters.clear();
	this->blobs.clear();
	this->params.clear();

	const f32 nval = neutral(this->config, this->heatmap);

	// Subtract the neutral value from the whole heatmap
	container::ops::transform(this->heatmap, this->neutralized,
				  [&](f32 value) { return std::max(value - nval, 0.0f); });

	const f32 athresh = this->config.activation_threshold / 255;
	const f32 dthresh = this->config.deactivation_threshold / 255;

	// Search for local maxima
	algorithms::find_local_maximas(this->neutralized, athresh, this->maximas);

	// Iterate over the maximas and start building clusters
	for (const index2_t point : this->maximas) {
		Cluster cluster =
			algorithms::span_cluster(this->neutralized, athresh, dthresh, point);

		this->clusters.push_back(std::move(cluster));
	}

	// Merge overlapping clusters
	algorithms::merge_overlaps(this->clusters, this->temp, 5);

	// Prepare clusters for gaussian fitting
	for (const Cluster &cluster : this->clusters) {
		const index2_t min = cluster.min();
		const index2_t max = cluster.max();
		const index2_t size = cluster.size();

		container::Image<f64> weights {size};

		const f64 x = min.x + ((size.x - 1) / 2.0);
		const f64 y = min.y + ((size.y - 1) / 2.0);

		const math::Vec2<f64> mean {x, y};
		const math::Mat2s<f64> prec {1.0, 0.0, 1.0};

		const gfit::BBox box {
			min.x,
			max.x,
			min.y,
			max.y,
		};

		gfit::Parameters<f64> params {
			true, 1, mean, prec, box, std::move(weights),
		};

		this->params.push_back(std::move(params));
	}

	gfit::fit(this->params, this->neutralized, this->fitting, 3);

	for (const auto &p : this->params) {
		if (!p.valid)
			continue;

		const std::optional<math::Mat2s<f64>> cov = p.prec.inverse();
		if (!cov.has_value())
			throw std::runtime_error("Failed to invert matrix!");

		this->blobs.push_back(Blob {p.mean + 0.5, cov.value()});
	}

	return this->blobs;
}

} // namespace iptsd::contacts::basic
