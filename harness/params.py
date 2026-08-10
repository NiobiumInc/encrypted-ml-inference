#!/usr/bin/env python3
"""
params.py - Parameters and directory structure for the submission.
"""
# Copyright 2025 Google LLC
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from pathlib import Path

# Enum for benchmark size. All sizes use ring 2^16 (128-bit) for the Niobium
# accelerator path.
SINGLE = 0   # 1 batch
SMALL  = 1   # 5 batches
MEDIUM = 2   # 20 batches

def instance_name(size):
    """Return the string name of the instance size."""
    names = ["single", "small", "medium"]
    if size >= len(names):
        return "unknown"
    return names[size]

class InstanceParams:
    """Parameters that differ for different instance sizes."""

    def __init__(self, size, rootdir=None):
        """Constructor."""
        self.size = size
        self.rootdir = Path(rootdir) if rootdir else Path.cwd()

        if size > MEDIUM:
            raise ValueError("Invalid instance size")

        #             single small  medium
        batch_size = [    1,     5,     20]

        self.batch_size = batch_size[size]

    def get_size(self):
        """Return the instance size."""
        return self.size

    # Directory structure methods
    def subdir(self):
        """Return the submission directory of this repository."""
        return self.rootdir

    def datadir(self):
        """Return the dataset directory path."""
        return self.rootdir / "datasets" / instance_name(self.size)
    
    def dataset_intermediate_dir(self):
        """Return the intermediate  directory path."""
        return self.datadir() / "intermediate"

    def iodir(self):
        """Return the I/O directory path."""
        return self.rootdir / "io" / instance_name(self.size)

    def io_intermediate_dir(self):
        """Return the intermediate  directory path."""
        return self.iodir() / "intermediate"

    def measuredir(self):
        """Return the measurements directory path."""
        return self.rootdir / "measurements" / instance_name(self.size)
    
    def get_batch_size(self):
        """Return the number of items in the batch."""
        return self.batch_size

    def get_test_input_file(self):
        """Return the test input file path."""
        return self.dataset_intermediate_dir() / "test_pixels.txt"

    def get_ground_truth_labels_file(self):
        """Return the ground truth labels file path."""
        return self.dataset_intermediate_dir() / "test_labels.txt"

    def get_encrypted_model_predictions_file(self):
        """Return the encrypted model predictions file path."""
        return self.iodir() / "encrypted_model_predictions.txt"

    def get_harness_model_predictions_file(self):
        """Return the harness model predictions file path."""
        return self.iodir() / "harness_model_predictions.txt"
