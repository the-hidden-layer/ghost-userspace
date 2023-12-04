import matplotlib.pyplot as plt
num_task = 1000
# Function to read data from a file
def read_data(file_path):
    with open(file_path, 'r') as file:
        lines = file.readlines()

    tasks_data = [lines[i:i + 5] for i in range(1, len(lines) - 1, 5)]
    
    # for t in tasks_data: print(t)

    tasks = [(float(task[2].split(":")[1].strip()[:-2]), float(task[3].split(":")[1].strip()[:-2])) for task in tasks_data]
    tasks.sort()
    
    return [e[1] for e in tasks]

# Function to calculate average service time
def calculate_average_service_time(tasks_data):
    total_service_time = sum(task for task in tasks_data)
    return total_service_time / len(tasks_data)

labels={
    'dyn':'ElasticSched',
    'cfs':'CFS',
    'fifo':'FIFO',
}

# Function to plot the results
def plot_results(schedulers, average_service_times):
    for scheduler, avg_service_time in zip(schedulers, average_service_times):
        plt.plot(range(1, num_task+1, 1), avg_service_time, label=labels[scheduler])

    plt.xlabel('Number of Tasks')
    plt.ylabel('Average Service Time (ms)')
    plt.title('Average Service Time Comparison ('+str(num_task)+')')
    plt.legend()
    plt.show()

# List of schedulers and corresponding file paths
schedulers = ['dyn', 'cfs', 'fifo']
file_paths = ['preemptiveWorkload_'+str(num_task)+'_dyn', 'preemptiveWorkload_'+str(num_task)+'_cfs', 'preemptiveWorkload_'+str(num_task)+'_fifo']

# Lists to store average service times for each scheduler
average_service_times = [[] for _ in schedulers]

# Loop through each scheduler and file path
for scheduler, file_path in zip(schedulers, file_paths):
    tasks_data = read_data('results/preemptiveWorkload/'+file_path)

    # Loop through different number of tasks
    for i in range(1, num_task+1):
        avg_service_time = calculate_average_service_time(tasks_data[:i])
        average_service_times[schedulers.index(scheduler)].append(avg_service_time)

# Plot the results
plot_results(schedulers, average_service_times)
