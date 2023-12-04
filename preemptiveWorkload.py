import os
import re
import matplotlib.pyplot as plt

def extract_total_service_time(file_path):
    with open(file_path, 'r') as file:
        content = file.read()

    # Extracting total service time using regular expression
    match = re.search(r'TotalServiceTime: (\d+\.\d+) ms', content)
    if match:
        return float(match.group(1))
    else:
        return None

labels = {
    'dyn': 'ElasticSched',
    'cfs': 'CFS',
    'fifo': 'FIFO',
}

def plot_comparison(results_folder):
    algorithms = ['dyn', 'cfs', 'fifo']
    task_counts = list(range(100, 1100, 100))

    total_service_times = {algo: [] for algo in algorithms}

    for algo in algorithms:
        for task_count in task_counts:
            file_name = f"preemptiveWorkload_{task_count}_{algo}"
            file_path = os.path.join(results_folder, f"{file_name}")

            total_service_time = extract_total_service_time(file_path)

            if total_service_time is not None:
                total_service_times[algo].append(total_service_time)

    # Plotting the results
    for algo in algorithms:
        plt.plot(task_counts, total_service_times[algo], label=labels[algo])

    plt.title('CPU Scheduler Algorithm Comparison')
    plt.xlabel('Number of Tasks')
    plt.ylabel('Total Service Time (ms)')
    plt.legend()
    plt.show()

if __name__ == "__main__":
    results_folder = 'results/preemptiveWorkload'
    plot_comparison(results_folder)
