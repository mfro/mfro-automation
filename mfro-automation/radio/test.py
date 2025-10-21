import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

# data = pd.read_csv('out2.csv', header=None)

# fig, axs = plt.subplots(2, 1, figsize=(100*2, 10), dpi=80)

# axs[0].margins(x=0)
# axs[0].plot(data.iloc[:, 1:])

# axs[1].margins(x=0)
# axs[1].twinx().plot(data[0])

# fig.savefig('output1.png', bbox_inches='tight', pad_inches=0)

data = pd.read_csv('out.csv', header=None)
# data2 = pd.read_csv('out2.csv', header=None)
# data3 = pd.read_csv('out3.csv', header=None)

# plt.figure(figsize=(100*2, 10), dpi=80)
# plt.imshow(data2, aspect=.01)
# plt.savefig("output2.png", bbox_inches='tight', pad_inches=0)

# plt.figure(figsize=(100*2, 10), dpi=80)
# plt.imshow(data3, aspect=.01)
# plt.savefig("output3.png", bbox_inches='tight', pad_inches=0)

fig, axs = plt.subplots(2, 1, figsize=(100*2, 10), dpi=80)
axs[0].margins(0)
axs[1].margins(0)

axs[0].plot(data.iloc[:, 0])
axs[0].twinx().plot(data.iloc[:, 1], color='C1')

# stride = len(data) // 36
# for index in range(36):

#   range = data.iloc[:, 1][index * stride:(index + 1) * stride]
#   axs[1].hist(range, bins=50, histtype='step')
# axs[1].plot(data2)

# axs[1].margins(0)
# axs[1].plot(data2)
# axs[1].xaxis.set_ticks(np.arange(len(data2), 10))
# plt.plot(data.iloc[:, 3])
# plt.plot(data.iloc[:, 4])
# plt.plot(data.iloc[:, 5])
# plt.ylim(0, 3)

# plt.twinx().plot(data.iloc[:, 2], color='C2')

plt.savefig("output1.png", bbox_inches='tight', pad_inches=0)
