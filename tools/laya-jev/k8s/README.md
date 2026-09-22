# Rancher Manager deployment

Rancher Manager deploys this as a normal Kubernetes workload. The image must
be pushed to a registry reachable by the cluster; Rancher cannot pull the
local `laya-jev:pr3` image from a developer laptop.

For a cluster without Hugging Face access, bake the verified standalone model
into a second image:

```sh
docker build -t laya-jev:pr3 tools/laya-jev
docker build -t ghcr.io/your-org/laya-jev:pr3-offline \
  -f tools/laya-jev/Dockerfile.preloaded \
  "$HOME/.cache/laya-jev/laya"
docker push ghcr.io/your-org/laya-jev:pr3-offline
```

Set `image` in `deployment.yaml` to the `pr3-offline` image and apply the
preloaded patch after the base workload:

```sh
kubectl -n laya-jev patch deployment laya-jev \
  --type strategic \
  --patch-file tools/laya-jev/k8s/deployment-preloaded-patch.yaml
```

That image contains the model under `/models/laya` and never contacts the Hub.
It is larger by roughly 807 MiB, so the regular PVC/cache deployment is more
efficient when the cluster has reliable model download access.

Build and push an image, replacing the registry and organization:

```sh
docker build -t ghcr.io/your-org/laya-jev:pr3 tools/laya-jev
docker push ghcr.io/your-org/laya-jev:pr3
```

Change `image` in `deployment.yaml` to that exact image reference. Create the
namespace and API secret, then apply the workload:

```sh
kubectl apply -f tools/laya-jev/k8s/namespace.yaml
kubectl -n laya-jev create secret generic laya-jev \
  --from-literal=LAYA_API_KEY='replace-with-a-long-secret'
kubectl apply -k tools/laya-jev/k8s
```

The service is cluster-internal. Verify it without publishing the endpoint:

```sh
kubectl -n laya-jev port-forward service/laya-jev 8768:8000
```

Use an Ingress or a LoadBalancer service when the callers live outside the
cluster. Keep the API key in the secret and pass it as the `Authorization:
Bearer ...` header.

The default workload is one CPU replica with a persistent model cache. The
container entrypoint prepares the model at `/cache/huggingface/laya-model` on
the first pod start and reuses it on later starts. The GPU patch requests one
NVIDIA GPU and sets `LAYA_DEVICE=cuda` on an already deployed workload:

```sh
kubectl -n laya-jev patch deployment laya-jev \
  --type strategic \
  --patch-file tools/laya-jev/k8s/gpu/deployment-gpu-patch.yaml
```

Use the GPU patch only on nodes with the NVIDIA device plugin and a Torch
build that includes CUDA support. Adjust the node selector or registry image
to match the cluster.
