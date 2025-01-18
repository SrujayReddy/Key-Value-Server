#!/usr/bin/python3
"""
A python script, that generates one file, called workload. 
The file is ascii text, and contain a request at each line. 
Keys and values are 4 byte long each. 
Each line of workload looks like this:
Req key value

For example:
put 4 8
get 3
get 4

We should be able to control the skew (zipf distribution), ratio of put/get requests, and the number of requests. So the call would look like the following:
./script -n num_reqs -s skew -r ratio_put_get
"""

import argparse
import random
import numpy as np
import matplotlib.pyplot as plt

show_plot = False
min_value = 1
max_value = int(4e9)


def generate_workload(num_reqs, skew, ratio_put_get):
    num_put = int(num_reqs * ratio_put_get)
    num_get = num_reqs - num_put
    # Generate the keys
    if skew >= 0 and skew <= 1:  # Uniform distribution
        keys = list(range(1, num_put + 1))
    else:  # zipf distribution
        keys = np.random.zipf(skew, num_put)
    np.random.shuffle(keys)

    if show_plot:
        plt.hist([key for key in keys if key < 20])
        plt.savefig("zipf.png")
    # Generate the values
    values = list(np.random.randint(min_value, max_value, num_put))
    # Replace zeros with non-zero values
    values = [v if v != 0 else 1 for v in values]
    # Generate the requests
    n, m = 0, 0
    requests = []
    while True:
        if random.random() < ratio_put_get and n < num_put:
            requests.append("put " + str(keys[n]) + " " + str(values[n]))
            n += 1
        elif m < num_get:
            i = random.randint(0, num_put - 1)
            requests.append("get " + str(keys[i]))
            m += 1
        if n == num_put and m == num_get:
            break
    return requests


def main():
    parser = argparse.ArgumentParser(description="Generate a workload")
    parser.add_argument("-n", type=int, default=100, help="Number of requests")
    parser.add_argument(
        "-s",
        type=float,
        default=0,
        help="Skew [0, 1] for uniform distribution, >1 for zipf distribution",
    )
    parser.add_argument("-r", type=float, default=0.5, help="Ratio of put/get requests")
    args = parser.parse_args()
    requests = generate_workload(args.n, args.s, args.r)
    with open("workload.txt", "w") as f:
        for i, request in enumerate(requests):
            f.write(request + "\n")
    print("Workload generated and saved to workload.txt")

    kvstore = {}
    with open("solution.txt", "w") as f:
        for request in requests:
            req = request.split()
            if req[0] == "put":
                kvstore[req[1]] = req[2]
                continue
            # get request
            val = 0
            if req[1] in kvstore:
                val = kvstore[req[1]]
            f.write(str(val) + "\n")

if __name__ == "__main__":
    main()
