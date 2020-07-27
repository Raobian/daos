#!/usr/bin/python
'''
  (C) Copyright 2020 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
'''
import time

from nvme_utils import ServerFillUp
from avocado.core.exceptions import TestFail
from general_utils import get_log_file, run_task
from daos_utils import DaosCommand
from apricot import skipForTicket

class NvmeEnospace(ServerFillUp):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: To validate DER_NOSPACE for SCM and NVMe
    :avocado: recursive
    """
    def der_enspace_log_count(self):
        """
        Function to count the DER_NOSPACE and other ERR in client log.

        returns:
            der_nospace_count(int): DER_NOSPACE count from client log.
            other_errors_count(int): Other Error count from client log.

        """
        #Get the Client side Error from client_log file.
        cmd = 'cat {} | grep ERR'.format(get_log_file(self.client_log))
        task = run_task(self.hostlist_clients, cmd)
        for _rc_code, _node in task.iter_retcodes():
            if _rc_code == 1:
                self.fail("Failed to run cmd {} on {}".format(cmd, _node))
        for buf, _nodes in task.iter_buffers():
            output = str(buf).split('\n')

        der_nospace_count = 0
        other_errors_count = 0
        for line in output:
            if 'DER_NOSPACE' in line:
                der_nospace_count += 1
            else:
                other_errors_count += 1

        return der_nospace_count, other_errors_count

    @skipForTicket("DAOS-4846")
    def test_scm_enospace(self):
        """Jira ID: DAOS-4756.

        Test Description: IO gets DER_NOSPACE when SCM is full and it release
                          the size when container destroy with Aggregation
                          disabled.

        Use Case: This tests will create the pool and disable aggregation. Fill
                  75% of SCM size which should work, next try fill 10% more
                  which should fail with DER_NOSPACE. Destroy the container
                  and validate the Pool SCM free size is close to full (> 95%).
                  Do this in loop ~10 times and verify the DER_NOSPACE and SCM
                  free size after container destroy.

        :avocado: tags=all,hw,medium,nvme,ib2,full_regression
        :avocado: tags=enospc_disable_aggregation, der_enospace
        """
        # pylint: disable=attribute-defined-outside-init
        # pylint: disable=too-many-branches
        expected_err_count = 0
        self.daos_cmd = DaosCommand(self.bin)
        self.create_pool_max_size()
        print(self.pool.pool_percentage_used())

        # Disable the aggregation
        self.pool.set_property("reclaim", "disabled")

        #Repeat the test in loop.
        for _loop in range(10):
            #Fill 75% of SCM pool
            self.start_ior_load(storage='SCM', precent=75)

            print(self.pool.pool_percentage_used())

            try:
                #Fill 10% more to SCM ,which should Fail because no SCM space
                self.start_ior_load(storage='SCM', precent=10)
                self.fail('This test suppose to because of DER_NOSPACE'
                          'Fail but it got Passed')
            except TestFail as _error:
                self.log.info('Test should get failed as expected')

            #Get the DER_NOSPACE and other error count from log
            der_nospace_count, other_error = self.der_enspace_log_count()

            #Check there are no other errors in log file
            if other_error > 0:
                self.fail('Found other count {} in client log {}'
                          .format(other_error, self.client_log))

            #Check the DER_NOSPACE error in log file
            expected_err_count += 1
            if der_nospace_count != expected_err_count:
                self.fail('Expected DER_NOSPACE should be {} and Found {}'
                          .format(expected_err_count, der_nospace_count))

            #List all the container
            kwargs = {"pool": self.pool.uuid, "svc": self.pool.svc_ranks}
            continers = (self.daos_cmd.get_output("pool_list_cont", **kwargs))
            #Destroy all the containers
            for _cont in continers:
                kwargs["cont"] = _cont
                self.daos_cmd.container_destroy(**kwargs)

            #Check the pool usage
            pool_usage = self.pool.pool_percentage_used()
            #Delay to release the SCM size.
            time.sleep(60)
            print(pool_usage)
            #SCM pool size should be released (some still be used for metadata)
            #Pool SCM free % should not be less than 95%
            if pool_usage['scm'] < 95:
                self.fail('SCM pool used percentage should be < 95, instead {}'.
                          format(pool_usage['scm']))

        #Run last IO
        self.start_ior_load(storage='SCM', precent=1)
